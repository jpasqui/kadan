// The import report, written as the import goes, and the ledger of what was not imported and why.

#include "P4KadanImportInternal.h"
#include "P4Common.h"
#include "P4ExportContainer.h"

using namespace P4KadanImport;

namespace P4KadanImport
{
FString JsonText(const TSharedRef<FJsonObject>& Object)
{
    FString Text;
    FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR>::Create(&Text));
    return Text;
}

TSharedRef<FJsonObject> NativeImportReport(bool Valid, const FString& Error, const FString& Package,
    const FString& AssetPath, bool Created, bool Saved, bool FilledStandIn, const FString& State)
{
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("valid"), Valid);
    Report->SetStringField(TEXT("error"), Error);
    Report->SetStringField(TEXT("package"), Package);
    Report->SetStringField(TEXT("asset"), AssetPath);
    Report->SetBoolField(TEXT("created"), Created);
    Report->SetBoolField(TEXT("saved"), Saved);
    Report->SetBoolField(TEXT("filled_standin"), FilledStandIn);
    Report->SetStringField(TEXT("state"), State);
    return Report;
}

} // namespace P4KadanImport

double P4KadanReportInterval = 5.0;

namespace
{
const double GigaBytes = 1024.0 * 1024.0 * 1024.0;

double Rounded(double Value)
{
    return FMath::RoundToDouble(Value * 100.0) / 100.0;
}
} // namespace

void FP4KadanImportRun::Spent(const FString& Step, double Since)
{
    Seconds.FindOrAdd(Step) += FPlatformTime::Seconds() - Since;
    ++Steps.FindOrAdd(Step);
    const double Used = double(FPlatformMemory::GetStats().UsedPhysical) / GigaBytes;
    if (LastUsedMemory > 0.0)
        MemoryGrowth.FindOrAdd(Step) += Used - LastUsedMemory;
    LastUsedMemory = Used;
    LowestAvailableMemory = FMath::Min(LowestAvailableMemory, double(P4AvailableCommitBytes()) / GigaBytes);
}

// With little memory left, another program (a second import, most often) gets a moment to finish before this one
// asks for more. The wait is limited for the whole run: on a computer that is simply short of memory it goes on.
void FP4KadanImportRun::WaitForMemory()
{
    const FString Setting = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_MEMORY_FLOOR_GB"));
    const double Floor = (Setting.IsEmpty() ? 4.0 : FCString::Atod(*Setting)) * GigaBytes;
    if (MemoryWaitLeft <= 0.0 || double(P4AvailableCommitBytes()) >= Floor)
        return;
    const double Started = FPlatformTime::Seconds();
    while (MemoryWaitLeft > 0.0 && double(P4AvailableCommitBytes()) < Floor && !(Progress && Progress->ShouldCancel()))
    {
        if (Progress)
            Progress->EnterProgressFrame(0.f);
        FPlatformProcess::Sleep(1.f);
        MemoryWaitLeft -= 1.0;
    }
    if (double(P4AvailableCommitBytes()) < Floor)
        Warnings.AddUnique(TEXT("This computer is low on memory, so this import could fail or be slow. Closing other programs helps."));
    Spent(TEXT("waiting for memory"), Started);
}

// The report is written as the import goes, so a stopped import still tells what it did. It is
// written at most every few seconds: writing the whole report (megabytes) after every asset made
// its cost grow with the square of the map's size.
void FP4KadanImportRun::Checkpoint(bool Force)
{
    const double CheckpointStarted = FPlatformTime::Seconds();
    if (!Force && CheckpointStarted - LastCheckpoint < P4KadanReportInterval)
        return;
    LastCheckpoint = CheckpointStarted;
    ON_SCOPE_EXIT
    {
        Spent(TEXT("report"), CheckpointStarted);
    };
    auto Timing = MakeShared<FJsonObject>(), Counted = MakeShared<FJsonObject>();
    for (const auto& Pair : Seconds)
        Timing->SetNumberField(Pair.Key, FMath::RoundToDouble(Pair.Value * 10.0) / 10.0);
    for (const auto& Pair : Steps)
        Counted->SetNumberField(Pair.Key, Pair.Value);
    Timing->SetNumberField(TEXT("total"), FMath::RoundToDouble((FPlatformTime::Seconds() - RunStarted) * 10.0) / 10.0);
    Result->SetObjectField(TEXT("seconds"), Timing);
    auto Memory = MakeShared<FJsonObject>(), Growth = MakeShared<FJsonObject>();
    const FPlatformMemoryStats MemoryNow = FPlatformMemory::GetStats();
    Memory->SetNumberField(TEXT("editor_peak_gb"), Rounded(double(MemoryNow.PeakUsedPhysical) / GigaBytes));
    Memory->SetNumberField(TEXT("editor_peak_commit_gb"), Rounded(double(MemoryNow.PeakUsedVirtual) / GigaBytes));
    if (LowestAvailableMemory < TNumericLimits<double>::Max())
        Memory->SetNumberField(TEXT("lowest_available_gb"), Rounded(LowestAvailableMemory));
    for (const auto& Pair : MemoryGrowth)
        Growth->SetNumberField(Pair.Key, Rounded(Pair.Value));
    Memory->SetObjectField(TEXT("growth_gb"), Growth);
    Result->SetObjectField(TEXT("memory"), Memory);
    Result->SetObjectField(TEXT("steps"), Counted);
    Result->SetArrayField(TEXT("problems"), Problems);
    Result->SetBoolField(TEXT("cancelled"), Cancelled);
    TArray<TSharedPtr<FJsonValue>> WarningValues;
    for (const FString& Warning : Warnings)
        WarningValues.Add(MakeShared<FJsonValueString>(Warning));
    Result->SetArrayField(TEXT("warnings"), WarningValues);
    TArray<TSharedPtr<FJsonValue>> ReferenceValues;
    for (const FString& Reference : Out.ReferenceOnly)
        ReferenceValues.Add(MakeShared<FJsonValueString>(Reference));
    Result->SetArrayField(TEXT("reference_only"), ReferenceValues);
    TArray<TSharedPtr<FJsonValue>> EmptyValues;
    for (const FString& Reference : Out.EmptyReferences)
        EmptyValues.Add(MakeShared<FJsonValueString>(Reference));
    Result->SetArrayField(TEXT("empty_references"), EmptyValues);
    TArray<TSharedPtr<FJsonValue>> KeptValues;
    for (const FString& Reference : Out.KeptAsTheGames)
        KeptValues.Add(MakeShared<FJsonValueString>(Reference));
    Result->SetArrayField(TEXT("kept_as_the_games"), KeptValues);
    auto ReferenceReasons = MakeShared<FJsonObject>();
    for (const auto& Pair : Out.GameReferenceReasons)
        ReferenceReasons->SetStringField(Pair.Key, Pair.Value);
    Result->SetObjectField(TEXT("game_reference_reasons"), ReferenceReasons);
    Result->SetNumberField(TEXT("original_identity_packages"), Out.OriginalIdentityPackages);
    Result->SetBoolField(TEXT("ok"), Problems.Num() == 0 && !Cancelled);
    const FString Temporary = File + TEXT(".tmp");
    if (!FFileHelper::SaveStringToFile(
            JsonText(Result), *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !IFileManager::Get().Move(*File, *Temporary, true, true))
    {
        if (!ReportWriteFailed)
            Problems.Add(MakeShared<FJsonValueString>(
                TEXT("Cannot save the import report. Check project Saved folder access before resuming.")));
        ReportWriteFailed = true;
        Result->SetArrayField(TEXT("problems"), Problems);
        Result->SetBoolField(TEXT("ok"), false);
    }
}

FP4KadanImportResult FP4KadanImportRun::Finish()
{
    ReleaseSavedLevels();
    // A package the plan did not find that the import then read from the game's containers and
    // imported is not missing: its warning is taken back.
    for (const auto& Pair : MissingWarnings)
        if (Out.Imported.Contains(Pair.Value.ToString()))
            Warnings.Remove(Pair.Key);
    for (const auto& Pair : KeptSources)
        if (!KeptHandled.Contains(Pair.Key))
            Warnings.AddUnique(Pair.Value->Package + TEXT(" (") + Pair.Value->Kind +
                TEXT(
                    ") is not imported with the map. Placed things do not need it in the editor, and the game keeps its own."));
    // What this run built goes into the library for the next project.
    const double Publishing = FPlatformTime::Seconds();
    if (const int32 Kept = Library.Publish(Out.Imported, Warnings))
    {
        Steps.FindOrAdd(TEXT("packages kept in the library")) = Kept;
        Spent(TEXT("library"), Publishing);
    }
    if (Library.Enabled())
        Result->SetStringField(TEXT("library"), Library.Folder());
    Checkpoint(true);
    Out.Cancelled = Cancelled;
    Out.Ok = Problems.Num() == 0 && !Cancelled;
    Out.Warnings = Warnings;
    for (const auto& Problem : Problems)
        Out.Problems.Add(Problem->AsString());
    Out.Report = JsonText(Result);
    return Out;
}

bool FP4KadanImportRun::VerifiedGameIdentity(const FP4KadanSourceAsset& Asset, FString& Detail)
{
    if (!ReferenceInventoryAttempted)
    {
        ReferenceInventoryAttempted = true;
        FP4ExportContainerTools Tools;
        Tools.Retoc = Settings.Retoc;
        Tools.Repak = Settings.Repak;
        if (Settings.GamePaks.IsEmpty() || Tools.Retoc.IsEmpty() || Tools.Repak.IsEmpty())
            ReferenceInventoryError = TEXT(
                "Original game identity verification requires the selected game containers, retoc and repak.");
        else
        {
            if (!MaterialRegistry.IsValid() && !MaterialRegistryAttempted)
            {
                MaterialRegistryAttempted = true;
                P4AcquireCurrentGameRegistry(Tools, Settings.GamePaks, Folder / TEXT("GameReferenceRegistry"),
                    MaterialRegistry, MaterialRegistryProblem);
            }
            if (!MaterialRegistry.IsValid())
                ReferenceInventoryError = MaterialRegistryProblem;
            else if (MaterialRegistry->IsCurrent(Tools, Settings.GamePaks, ReferenceInventoryError))
                P4ReadExportGameInventory(Tools, Settings.GamePaks,
                    FPaths::ProjectSavedDir() / TEXT("Kadan/GameReferenceInventory"), ReferenceInventory,
                    ReferenceInventoryError);
        }
    }
    if (!ReferenceInventory.IsValid() || !ReferenceInventory.Contains(FName(*Asset.Package)))
    {
        Detail = ReferenceInventoryError.IsEmpty()
            ? TEXT("The selected game containers do not contain this original package.")
            : ReferenceInventoryError;
        return false;
    }
    TArray<uint8> Header;
    const int64 Size = IFileManager::Get().FileSize(*Asset.Header),
                BodySize = IFileManager::Get().FileSize(*Asset.Body);
    if (Size < 0 || Size > 64 * 1024 * 1024 || BodySize < 0 || BodySize > 64 * 1024 * 1024 ||
        !FFileHelper::LoadFileToArray(Header, *Asset.Header, FILEREAD_Silent))
    {
        Detail = TEXT(
            "The dependency input is unreadable; only its independently verified original game identity is retained.");
        return true;
    }
    const auto Source = P4MapKit::ReadPackageHeader(
        std::vector<uint8>(Header.GetData(), Header.GetData() + Header.Num()), BodySize);
    if (!Source.Error.Code.empty() || FString(UTF8_TO_TCHAR(Source.Package.c_str())) != Asset.Package)
    {
        Detail = TEXT(
            "The dependency input has an invalid or mismatched package header; only its independently verified original game identity is retained.");
        return true;
    }
    const FString Leaf = FPackageName::GetShortName(Asset.Package);
    bool RootFound = false;
    for (const auto& Export : Source.Exports)
        if (Export.Outer == 0 &&
            (FString(UTF8_TO_TCHAR(Export.Name.c_str())) == Leaf ||
                FString(UTF8_TO_TCHAR(Export.Name.c_str())) == Leaf + TEXT("_C")))
        {
            std::string Class;
            RootFound = Source.Reference(Export.Class, Class) && !Class.empty();
            if (RootFound)
                break;
        }
    Detail = RootFound
        ? TEXT(
              "Canonical header/export ranges and the original game package identity are verified. Its unsupported payload is not reconstructed.")
        : TEXT(
              "The dependency input root identity is unusable; only its independently verified original game identity is retained.");
    TArray<uint8> Body;
    FString ReadDigest;
    if (FFileHelper::LoadFileToArray(Body, *Asset.Body, FILEREAD_Silent) && Body.Num() == BodySize &&
        P4HashSourceBuffers(Header.GetData(), Header.Num(), Body.GetData(), Body.Num(), ReadDigest))
        Detail += TEXT(" Input snapshot SHA256: ") + ReadDigest +
            TEXT("; this does not assert equality with the game's payload.");
    return true;
}

void FP4KadanImportRun::Failed(
    const FP4KadanSourceAsset& Asset, const FString& Text, bool Fatal, bool MayKeepGameIdentity)
{
    // Declaration-bound dependencies can fail before ImportOne produced a
    // reply. Keep their concrete asset kind and refusal in the same ledger.
    if (!Replies->HasField(Asset.Package))
    {
        auto Reply = NativeImportReport(
            false, Text, Asset.Package, FString(), false, false, false, TEXT("not imported"));
        Reply->SetStringField(TEXT("kind"), Asset.Kind);
        Replies->SetObjectField(Asset.Package, Reply);
    }
    NotImported.Add(FName(*Asset.Package));
    Out.NotImported.AddUnique(Asset.Package);
    if (BestEffort && P4KadanKeptAsTheGames(Asset.Kind))
        StaysTheGames.Add(FName(*Asset.Package));
    FString IdentityDetail;
    if (Fatal && (MayKeepGameIdentity || BestEffort) && Asset.Kind != TEXT("World") &&
        VerifiedGameIdentity(Asset, IdentityDetail))
        Fatal = false;
    const FString Detail = Asset.Package + TEXT(" was not reconstructed. ") +
        (Text.IsEmpty() ? TEXT("The saved asset report says why.") : Text) +
        (IdentityDetail.IsEmpty() ? FString() : TEXT(" ") + IdentityDetail);
    if (BestEffort && Fatal && Asset.Kind != TEXT("World"))
    {
        GameBackedPackages.Add(FName(*Asset.Package));
        Warnings.AddUnique(Detail + TEXT(" It is left out, and levels keep the actors that need it in the game."));
    }
    else if (Fatal || Asset.Kind == TEXT("World"))
    {
        // Best effort records a level that could not be made at all and goes on with the others.
        Problems.Add(MakeShared<FJsonValueString>(Detail));
        FatalFailure = FatalFailure || !BestEffort;
    }
    else
    {
        GameBackedPackages.Add(FName(*Asset.Package));
        Out.ReferenceOnly.AddUnique(Asset.Package);
        Out.KeptAsTheGames.AddUnique(Asset.Package);
        // A static mesh this editor cannot rebuild the game's way (its engine build differs, a collision
        // shape it cannot hold) stays the game's, and what names it still imports: a Blueprint keeps the
        // game's value for it with a note, as for any game asset the project does not hold. Otherwise a
        // cave chunk whose air collision mesh is refused never imports and its level shows nothing there.
        if (BestEffort && Asset.Kind == TEXT("StaticMesh"))
            StaysTheGames.Add(FName(*Asset.Package));
        Out.GameReferenceReasons.Add(
            Asset.Package, Text + (IdentityDetail.IsEmpty() ? FString() : TEXT(" ") + IdentityDetail));
        const TSharedPtr<FJsonObject>* SourceReply = nullptr;
        if (Replies->TryGetObjectField(Asset.Package, SourceReply))
        {
            (*SourceReply)->SetBoolField(TEXT("kept_as_the_games"), true);
            (*SourceReply)->SetBoolField(TEXT("reference_only"), true);
            (*SourceReply)->SetStringField(TEXT("state"), TEXT("original game reference"));
        }
        Warnings.AddUnique(Detail + TEXT(" The original game asset is preserved."));
    }
}

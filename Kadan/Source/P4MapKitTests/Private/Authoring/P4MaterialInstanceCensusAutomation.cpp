#include "P4KadanImport.h"
#include "P4MaterialInstance.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "PackageTools.h"
#include "P4MapKitCore/material_source_import_compare.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialInstanceImportCensus, "P4MapKit.Quality.MaterialInstanceImportCensus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialInstanceImportCensus::RunTest(const FString&)
{
    const FString Folder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_CENSUS_SOURCE"));
    if (Folder.IsEmpty())
    {
        AddInfo(TEXT("P4MapKit test not run. Set P4_MI_CENSUS_SOURCE to the extracted game's material universe."));
        return true;
    }
    const FString Output = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_CENSUS_RECEIPT"));
    if (!TestFalse(TEXT("Census needs a receipt destination"), Output.IsEmpty()))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Folder;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    Settings.ShaderArchive = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_CENSUS_SHADER_ARCHIVE"));
    Settings.AllowGameBackedLevels = true;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    if (!TestTrue(TEXT("Scan complete source universe"), P4KadanScanGame(Folder, Sources, Problems)))
        return false;
    TArray<FString> Selected;
    TMap<FString, TSharedPtr<FP4KadanSourceAsset>> OriginalFiles;
    for (const auto& Source : Sources)
        if (Source->Kind == TEXT("MaterialInstanceConstant"))
        {
            Selected.Add(Source->Package);
            OriginalFiles.Add(Source->Package, Source);
        }
    Selected.Sort();
    if (!TestTrue(TEXT("Original MICs found"), Selected.Num() > 0))
        return false;
    TSharedRef<FJsonObject> Receipt = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Rows;
    int32 Intact = 0, Missing = 0, Different = 0, ReconstructedIntact = 0, ReferenceIntact = 0, SourceBoundIntact = 0;
    bool Cancelled = false;
    const bool ReadbackOnly = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_CENSUS_READBACK_ONLY")) == TEXT("1");
    // A long normal run may end at its owned process deadline. Continue its
    // completed prefix without repeating recovery or rewriting saved assets.
    // This is report continuity, not import authority. The final readback-only
    // pass still examines every original and every saved native asset afresh.
    const FString PriorFile = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_CENSUS_PRIOR_RECEIPT"));
    int32 FirstPending = 0;
    if (!PriorFile.IsEmpty())
    {
        FString PriorText, PriorHash;
        TSharedPtr<FJsonObject> Prior;
        const TArray<TSharedPtr<FJsonValue>>* PriorRows = nullptr;
        double PriorSelected = 0, PriorExamined = 0;
        bool WasReadback = true;
        if (!TestTrue(TEXT("Continuation requires a bounded normal-import receipt"),
                !ReadbackOnly && FFileHelper::LoadFileToString(PriorText, *PriorFile) &&
                    PriorText.Len() < 16 * 1024 * 1024 &&
                    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PriorText), Prior) && Prior.IsValid() &&
                    Prior->TryGetBoolField(TEXT("readback_only"), WasReadback) && !WasReadback &&
                    Prior->TryGetNumberField(TEXT("selected"), PriorSelected) && PriorSelected == Selected.Num() &&
                    Prior->TryGetNumberField(TEXT("examined"), PriorExamined) &&
                    Prior->TryGetArrayField(TEXT("rows"), PriorRows) && PriorRows->Num() == PriorExamined &&
                    PriorRows->Num() > 0 && PriorRows->Num() < Selected.Num() && P4HashUtf8(PriorText, PriorHash)))
            return false;
        for (int32 At = 0; At < PriorRows->Num(); ++At)
        {
            const auto Row = (*PriorRows)[At]->AsObject();
            FString Package, Status;
            bool Native = false, Bound = false;
            if (!TestTrue(TEXT("Continuation rows must be the exact selected prefix"),
                    Row.IsValid() && Row->TryGetStringField(TEXT("package"), Package) &&
                        P4ExactNativeString(Package, Selected[At]) && Row->TryGetStringField(TEXT("status"), Status) &&
                        (Status == TEXT("all-overrides-intact") || Status == TEXT("not-imported") ||
                            Status == TEXT("readback-differs"))))
                return false;
            if (Status == TEXT("all-overrides-intact"))
            {
                ++Intact;
                if (Row->TryGetBoolField(TEXT("native_reconstructed"), Native))
                {
                    if (Native)
                        ++ReconstructedIntact;
                    else
                        ++ReferenceIntact;
                }
                Row->TryGetBoolField(TEXT("source_bound_native"), Bound);
                if (Native && Bound)
                    ++SourceBoundIntact;
            }
            else if (Status == TEXT("not-imported"))
                ++Missing;
            else
                ++Different;
            Rows.Add((*PriorRows)[At]);
        }
        FirstPending = Rows.Num();
        Receipt->SetNumberField(TEXT("continued_prefix"), FirstPending);
        Receipt->SetStringField(TEXT("prior_receipt_text_sha256"), PriorHash);
    }
    const FString BatchSetting = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_CENSUS_BATCH"));
    const int32 Batch = BatchSetting.IsEmpty() ? 512 : FMath::Clamp(FCString::Atoi(*BatchSetting), 1, 1024);
    const auto Save = [&]()
    {
        Receipt->SetNumberField(TEXT("selected"), Selected.Num());
        Receipt->SetNumberField(TEXT("examined"), Rows.Num());
        Receipt->SetNumberField(TEXT("all_overrides_intact"), Intact);
        Receipt->SetNumberField(TEXT("not_imported"), Missing);
        Receipt->SetNumberField(TEXT("readback_differs"), Different);
        Receipt->SetArrayField(TEXT("rows"), Rows);
        Receipt->SetNumberField(TEXT("reconstructed_overrides_intact"), ReconstructedIntact);
        Receipt->SetNumberField(TEXT("reference_or_unqualified_records_intact"), ReferenceIntact);
        Receipt->SetNumberField(TEXT("source_bound_native_overrides_intact"), SourceBoundIntact);
        Receipt->SetBoolField(TEXT("classification_complete"), ReconstructedIntact + ReferenceIntact == Intact);
        Receipt->SetNumberField(TEXT("batch_size"), Batch);
        Receipt->SetBoolField(TEXT("readback_only"), ReadbackOnly);
        Receipt->SetBoolField(TEXT("cancelled"), Cancelled);
        Receipt->SetBoolField(TEXT("complete"), !Cancelled && Rows.Num() == Selected.Num());
        Receipt->SetStringField(TEXT("scope"),
            ReadbackOnly
                ? TEXT(
                      "Readback only; no import in this pass. Compare the original cooked local-override projection with native editor JSON and classify original metadata. The project may be partial: the separate normal-import receipt determines coverage. C4 flags are transport records, not stock UE properties. No renderer parity claim.")
                : TEXT(
                      "Normal Kadan importer followed by original cooked local-override projection versus native editor instance JSON. Any continued prefix retains the prior receipt's completed rows; this pass imports the remaining suffix. The separate final readback examines every saved asset afresh. C4 game overrides are checked as retained transport records, not stock UE properties. No renderer parity claim."));
        FString Text;
        FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<>::Create(&Text));
        return FFileHelper::SaveStringToFile(Text, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    };
    // Bounded reports avoid rewriting a game-wide import-result document after every asset.
    // A normal import recovers a complete master family. Reuse that work across
    // a bounded group rather than reopening/recovering the family every 64 MICs.
    // Resume preserves packages already imported into this census project.
    for (int32 First = FirstPending; First < Selected.Num(); First += Batch)
    {
        TSet<FString> Roots;
        for (int32 At = First; At < FMath::Min(First + Batch, Selected.Num()); ++At)
            Roots.Add(Selected[At]);
        FP4KadanImportResult Imported;
        if (!ReadbackOnly)
        {
            const auto Plan = P4KadanPlanImport(Sources, Roots, true, true);
            Imported = P4KadanRunImport(Settings, Plan, true);
        }
        if (Imported.Cancelled)
        {
            Cancelled = true;
            Save();
            AddError(TEXT("Normal import cancelled; census is incomplete"));
            return false;
        }
        for (int32 At = First; At < FMath::Min(First + Batch, Selected.Num()); ++At)
        {
            TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("package"), Selected[At]);
            auto* Instance = LoadObject<UMaterialInstanceConstant>(nullptr,
                *(Selected[At] + TEXT(".") + FPaths::GetBaseFilename(Selected[At])), nullptr, LOAD_NoWarn | LOAD_Quiet);
            FString Error, Status;
            if (!Instance)
            {
                ++Missing;
                Status = TEXT("not-imported");
                for (const auto& Problem : Imported.Problems)
                    if (Problem.Contains(Selected[At]) && Error.Len() < 4096)
                        Error += Problem + TEXT("\n");
                if (Error.IsEmpty())
                    Error = TEXT(
                        "No native instance was created by the normal importer; see its retained import-result report.");
            }
            else
            {
                const auto OriginalStatus = P4QueryOriginalAsset(Instance);
                const bool NativeState = OriginalStatus.Record.ComparisonSchema ==
                    TEXT("material-instance-native-state-v1");
                const bool Reconstructed = OriginalStatus.IsOriginal() && OriginalStatus.Record.Version == 2 &&
                    !OriginalStatus.Record.ReferenceOnly && OriginalStatus.State != EP4OriginalState::NeedsMigration &&
                    OriginalStatus.State != EP4OriginalState::Unreadable &&
                    OriginalStatus.State != EP4OriginalState::UnsupportedRecord &&
                    OriginalStatus.Record.SourceDigestKind == TEXT("material-instance-v2-md5") &&
                    (NativeState || OriginalStatus.Record.ComparisonSchema == TEXT("material-instance-v2"));
                Row->SetBoolField(TEXT("native_reconstructed"), Reconstructed);
                Row->SetBoolField(TEXT("source_bound_native"), Reconstructed && NativeState);
                Row->SetBoolField(TEXT("reference_only"), OriginalStatus.Record.ReferenceOnly);
                Row->SetStringField(TEXT("source_digest_kind"), OriginalStatus.Record.SourceDigestKind);
                Row->SetStringField(TEXT("comparison_schema"), OriginalStatus.Record.ComparisonSchema);
                Row->SetStringField(TEXT("fidelity"), OriginalStatus.Record.Fidelity);
                const FString Actual = P4MaterialInstanceJson(Instance, Error);
                FString ExpectedText;
                bool Equal = false;
                try
                {
                    if (Error.IsEmpty())
                    {
                        const auto Original = OriginalFiles.FindRef(Selected[At]);
                        TArray<uint8> Header, Body;
                        if (!Original.IsValid() || !FFileHelper::LoadFileToArray(Header, *Original->Header) ||
                            !FFileHelper::LoadFileToArray(Body, *Original->Body) || Header.Num() > 64 * 1024 * 1024 ||
                            Body.Num() > 64 * 1024 * 1024)
                            throw std::runtime_error("Original MIC files are missing or exceed the inspector bound");
                        const std::vector<uint8> H(Header.GetData(), Header.GetData() + Header.Num()),
                            B(Body.GetData(), Body.GetData() + Body.Num());
                        const auto Package = P4MapKit::ReadPackageHeader(H, B.size());
                        P4MapKit::FMaterialCookedRecord Record;
                        P4MapKit::FReadError ReadError;
                        if (!P4MapKit::ReadMaterialCookedRecord(Package, B, Record, ReadError) ||
                            Record.Class != "MaterialInstanceConstant")
                            throw std::runtime_error("Original MIC cannot be decoded: " + ReadError.Code);
                        Record.Package = TCHAR_TO_UTF8(*Selected[At]);
                        const auto Expected = P4MapKit::Material::ProjectOriginalMaterialInstanceForImport(Record);
                        ExpectedText = UTF8_TO_TCHAR(Expected.Dump().c_str());
                        Equal = P4MapKit::Material::CompareOriginalMaterialInstanceImport(
                            Expected, P4MapKit::Material::Json::Parse(TCHAR_TO_UTF8(*Actual)));
                    }
                }
                catch (const std::exception& Exception)
                {
                    Error = UTF8_TO_TCHAR(Exception.what());
                }
                if (Equal)
                {
                    ++Intact;
                    if (Reconstructed)
                        ++ReconstructedIntact;
                    else
                        ++ReferenceIntact;
                    if (Reconstructed && NativeState)
                        ++SourceBoundIntact;
                    Status = TEXT("all-overrides-intact");
                }
                else
                {
                    ++Different;
                    Status = TEXT("readback-differs");
                    if (Error.IsEmpty())
                        Error = TEXT("Native override readback differs from the original cooked projection");
                    Row->SetStringField(TEXT("expected_json"), ExpectedText);
                    Row->SetStringField(TEXT("actual_json"), Actual);
                }
            }
            Row->SetStringField(TEXT("status"), Status);
            Row->SetStringField(TEXT("error"), Error);
            Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
        if (!Save())
        {
            AddError(TEXT("Cannot write bounded census checkpoint"));
            return false;
        }
        UE_LOG(LogTemp, Display, TEXT("MIC census %d/%d: %d intact, %d missing, %d differ"), Rows.Num(), Selected.Num(),
            Intact, Missing, Different);
        TArray<UPackage*> Packages;
        for (TObjectIterator<UPackage> It; It; ++It)
            if (It->GetName().StartsWith(TEXT("/Game/Carrot4/")) && !It->IsDirty())
                Packages.Add(*It);
        UPackageTools::UnloadPackages(Packages);
    }
    return TestEqual(TEXT("All selected original instances examined"), Rows.Num(), Selected.Num());
}

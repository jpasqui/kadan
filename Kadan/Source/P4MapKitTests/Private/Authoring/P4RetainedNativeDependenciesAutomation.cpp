#include "P4KadanImport.h"
#include "P4OriginalRecord.h"
#include "P4RecoveredParticleSystem.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Particles/ParticleModuleRequired.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RetainedNativeDependencies, "P4MapKit.Kadan.RetainedNativeDependencies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RetainedNativeDependencies::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETAINED_KEEP")) != TEXT("1"))
    {
        AddWarning(
            TEXT("P4MapKit test not run. Opt-in retained original Blueprint/DataTable/particle fixture is required."));
        return true;
    }
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/RetainedKeptSource");
    Settings.ConvertedCache = FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals");
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    const FString Root = TEXT("/Game/Carrot4/Placeables/Gimmicks/CrackPot/GCrackPotL");
    const FString Table = TEXT("/Game/Carrot4/Audio/AudioData/DT_AudioData_CrackPotL");
    const FString Particle = TEXT("/Game/Carrot4/Effects/Objects/Gimmicks/CrackPot/PS_eft_CrackPotL_Break00");
    const auto ObjectPath = [](const FString& Package)
    { return Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package); };
    TArray<TSharedPtr<FP4KadanSourceAsset>> Source;
    TArray<FString> ScanProblems;
    if (!TestTrue(TEXT("The source fixture holds only the selected original Blueprint"),
            P4KadanScanGame(Settings.GameFolder, Source, ScanProblems) && !ScanProblems.Num() && Source.Num() == 1 &&
                Source[0]->Package == Root))
        return false;
    UObject* ExistingTable = LoadObject<UObject>(nullptr, *ObjectPath(Table));
    UObject* ExistingParticle = LoadObject<UObject>(nullptr, *ObjectPath(Particle));
    if (!TestTrue(TEXT("Both dependency packages already exist; no fresh dependency import is substituted"),
            ExistingTable && ExistingParticle && FPackageName::DoesPackageExist(Table) &&
                FPackageName::DoesPackageExist(Particle)))
        return false;
    const auto TableBefore = P4DiagnoseOriginalAsset(ExistingTable);
    TStrongObjectPtr<UParticleSystem> ParticleControl(Cast<UParticleSystem>(ExistingParticle));
    const auto ParticleOriginal = P4DiagnoseOriginalAsset(ExistingParticle);
    auto* CreatorInput = ParticleControl.IsValid()
        ? Cast<UParticleModuleRequired>(P4FindOriginalParticleObject(
              ParticleControl.Get(), TEXT("PS_eft_CrackPotL_Break00.ParticleModuleRequired_12")))
        : nullptr;
    if (!TestTrue(TEXT("Native custody first restores the retained particle's exact original baseline"),
            ParticleOriginal.Error.IsEmpty() && ParticleOriginal.Comparison == EP4OriginalComparison::Equal &&
                ParticleOriginal.Provenance.Record.ReferenceOnly && CreatorInput))
        return false;
    const float OriginalDuration = CreatorInput->EmitterDuration;
    const bool OriginalDirty = ParticleControl->GetOutermost()->IsDirty();
    ON_SCOPE_EXIT
    {
        CreatorInput->EmitterDuration = OriginalDuration;
        int32 Restored = 0;
        FString Error;
        if (!P4RestoreOriginalParticleCutouts(ParticleControl.Get(), Restored, Error))
            AddError(Error);
        ParticleControl->GetOutermost()->SetDirtyFlag(OriginalDirty);
    };
    // Keep the real creator-change guard covered after native load drift is repaired.
    CreatorInput->EmitterDuration = OriginalDuration + .125f;
    const auto ParticleBefore = P4DiagnoseOriginalAsset(ExistingParticle);
    if (!TestTrue(
            TEXT(
                "The retained table is qualified and the deliberately edited particle remains an original read-only control"),
            TableBefore.Error.IsEmpty() && TableBefore.Comparison == EP4OriginalComparison::Equal &&
                ParticleBefore.Error.IsEmpty() && ParticleBefore.Comparison == EP4OriginalComparison::Different &&
                ParticleBefore.Provenance.Record.ReferenceOnly))
        return false;
    ExistingTable = nullptr;
    ExistingParticle = nullptr;
    auto Receipt = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Passes;
    Receipt->SetStringField(TEXT("particle_recorded_state"), ParticleBefore.Provenance.Record.ComparisonDigest);
    Receipt->SetStringField(TEXT("particle_current_state"), ParticleBefore.CurrentDigest);
    Receipt->SetStringField(TEXT("particle_source_digest"), ParticleBefore.Provenance.Record.SourceDigest);
    TArray<TSharedPtr<FJsonValue>> ParticleSamples;
    for (const TCHAR* Package : {TEXT("/Game/Carrot4/Effects/Cmn/PS_eft_Cmn_In00"),
             TEXT("/Game/Carrot4/Effects/Objects/Items/ShugoFlag/PS_eft_ShugoExFlag_Set00"),
             TEXT("/Game/Carrot4/Effects/Objects/Madori/PS_eft_DungeonExit_Light01")})
    {
        const auto Sample = P4DiagnoseOriginalAsset(LoadObject<UObject>(nullptr, *ObjectPath(Package)));
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), Package);
        Row->SetBoolField(TEXT("original"), Sample.Provenance.IsOriginal());
        Row->SetBoolField(TEXT("reference_only"), Sample.Provenance.Record.ReferenceOnly);
        Row->SetStringField(TEXT("comparison"),
            Sample.Comparison == EP4OriginalComparison::Equal           ? TEXT("equal")
                : Sample.Comparison == EP4OriginalComparison::Different ? TEXT("different")
                                                                        : TEXT("not_compared"));
        Row->SetStringField(TEXT("error"), Sample.Error);
        Row->SetStringField(TEXT("recorded_state"), Sample.Provenance.Record.ComparisonDigest);
        Row->SetStringField(TEXT("current_state"), Sample.CurrentDigest);
        Row->SetStringField(TEXT("source_digest"), Sample.Provenance.Record.SourceDigest);
        ParticleSamples.Add(MakeShared<FJsonValueObject>(Row));
    }
    Receipt->SetArrayField(TEXT("particle_samples"), ParticleSamples);
    ON_SCOPE_EXIT
    {
        Receipt->SetArrayField(TEXT("passes"), Passes);
        Receipt->SetBoolField(TEXT("passed"), !HasAnyErrors());
        FString Text;
        FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<TCHAR>::Create(&Text));
        const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/RetainedKept/result.json");
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        if (!FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            AddError(TEXT("Retained dependency receipt could not be saved."));
    };
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        const auto Result = P4KadanRunImport(Settings, P4KadanPlanImport(Settings, {Root}, false), false);
        TSharedPtr<FJsonObject> Report;
        if (!TestTrue(TEXT("Both true reimport passes finish without dependency omissions"),
                Result.Ok && !Result.NotImported.Num() &&
                    FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Result.Report), Report) &&
                    Report.IsValid()))
            return false;
        const TSharedPtr<FJsonObject>* Assets = nullptr;
        const TSharedPtr<FJsonObject>* TableReply = nullptr;
        const TSharedPtr<FJsonObject>* ParticleReply = nullptr;
        if (!TestTrue(TEXT("The retained table dependency has an actual per-asset reply"),
                Report->TryGetObjectField(TEXT("assets"), Assets) && (*Assets)->TryGetObjectField(Table, TableReply)))
            return false;
        if (!TestTrue(TEXT("The original recovered-particle helper is discovered as a retained dependency"),
                (*Assets)->TryGetObjectField(Particle, ParticleReply)))
            return false;
        bool TableValid = false, TableSaved = false, TableReference = true, ParticleValid = false, ParticleSaved = true,
             ParticleReference = false;
        FString ParticleState;
        (*TableReply)->TryGetBoolField(TEXT("valid"), TableValid);
        (*TableReply)->TryGetBoolField(TEXT("saved"), TableSaved);
        (*TableReply)->TryGetBoolField(TEXT("reference_only"), TableReference);
        if (ParticleReply)
        {
            (*ParticleReply)->TryGetBoolField(TEXT("valid"), ParticleValid);
            (*ParticleReply)->TryGetBoolField(TEXT("saved"), ParticleSaved);
            (*ParticleReply)->TryGetBoolField(TEXT("reference_only"), ParticleReference);
            (*ParticleReply)->TryGetStringField(TEXT("state"), ParticleState);
        }
        const auto TableAfter = P4DiagnoseOriginalAsset(LoadObject<UObject>(nullptr, *ObjectPath(Table)));
        const auto ParticleAfter = P4DiagnoseOriginalAsset(LoadObject<UObject>(nullptr, *ObjectPath(Particle)));
        auto One = MakeShared<FJsonObject>();
        One->SetNumberField(TEXT("pass"), Pass + 1);
        One->SetObjectField(TEXT("table"), *TableReply);
        One->SetBoolField(TEXT("particle_reply_present"), ParticleReply != nullptr);
        if (ParticleReply)
            One->SetObjectField(TEXT("particle"), *ParticleReply);
        One->SetNumberField(TEXT("not_imported"), Result.NotImported.Num());
        One->SetBoolField(TEXT("table_cold_equal"),
            TableAfter.Error.IsEmpty() && TableAfter.Comparison == EP4OriginalComparison::Equal);
        One->SetBoolField(TEXT("particle_unmodified"),
            ParticleAfter.Error.IsEmpty() && ParticleAfter.CurrentDigest == ParticleBefore.CurrentDigest);
        Passes.Add(MakeShared<FJsonValueObject>(One));
        if (!TestTrue(TEXT("Each retained table pass actually saves through native cold reimport and remains exact"),
                TableValid && TableSaved && !TableReference && TableAfter.Error.IsEmpty() &&
                    TableAfter.Comparison == EP4OriginalComparison::Equal))
            return false;
        if (!TestTrue(TEXT("Differing retained particle stays preserved and read-only rather than falsely upgraded"),
                (!ParticleReply ||
                    (ParticleValid && !ParticleSaved && ParticleReference &&
                        ParticleState == TEXT("existing creator content preserved"))) &&
                    ParticleAfter.Error.IsEmpty() && ParticleAfter.CurrentDigest == ParticleBefore.CurrentDigest &&
                    ParticleAfter.Provenance.Record.ReferenceOnly))
            return false;
    }
    return true;
}
#endif

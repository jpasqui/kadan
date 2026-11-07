#include "P4KadanImport.h"
#include "P4KadanSourceCatalog.h"
#include "P4CurrentGameRegistry.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialShared.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "ShaderCompiler.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace P4MaterialCensus
{
// The import's own answer for one material package of a batch report: editable, kept for reference
// only (with the refusal), or not imported (with why).
TSharedRef<FJsonObject> Row(
    const FString& Package, const TSharedPtr<FJsonObject>& Reply, const FP4KadanImportResult& Result)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("package"), Package);
    FString Kind, Error, Refusal, State;
    bool Valid = false, ReferenceOnly = false;
    if (Reply.IsValid())
    {
        Reply->TryGetStringField(TEXT("kind"), Kind);
        Reply->TryGetBoolField(TEXT("valid"), Valid);
        Reply->TryGetBoolField(TEXT("reference_only"), ReferenceOnly);
        Reply->TryGetStringField(TEXT("error"), Error);
        Reply->TryGetStringField(TEXT("state"), State);
        if (!Reply->TryGetStringField(TEXT("recovered_graph_error"), Refusal))
            Reply->TryGetStringField(TEXT("first_refusal"), Refusal);
    }
    if (const FString* Why = Result.GameReferenceReasons.Find(Package))
        if (Refusal.IsEmpty())
            Refusal = *Why;
    Out->SetStringField(TEXT("kind"), Kind);
    const bool NotImported = !Reply.IsValid() || Result.NotImported.Contains(Package) || !Valid;
    Out->SetStringField(TEXT("state"),
        NotImported                                                   ? TEXT("not_imported")
            : ReferenceOnly || Result.ReferenceOnly.Contains(Package) ? TEXT("reference_only")
                                                                      : TEXT("editable"));
    Out->SetStringField(TEXT("reason"), !Refusal.IsEmpty() ? Refusal : Error);
    Out->SetStringField(TEXT("import_state"), State);
    return Out;
}
// The renderer's compile result of a built game material, empty when it compiled.
FString CompileError(const FString& Package)
{
    const FString Object = Package + TEXT(".") + FPaths::GetBaseFilename(Package);
    UMaterialInterface* Material = FindObject<UMaterialInterface>(nullptr, *Object);
    if (!Material)
        Material = LoadObject<UMaterialInterface>(nullptr, *Object, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!Material)
        return TEXT("not loaded");
    FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIFeatureLevel);
    if (!Resource)
        return TEXT("no material resource");
    if (Resource->GetCompileErrors().Num())
        return Resource->GetCompileErrors()[0];
    if (!Resource->HasValidGameThreadShaderMap())
        return TEXT("no shader map");
    return FString();
}
} //namespace P4MaterialCensus

// Every game material through Kadan's normal import, the way the Kadan panel runs it from the game's Paks
// folder: each Material and MaterialInstanceConstant the game's registry names, in batches, resume on, then
// (with a renderer) the editor's compile result of each one built. Opt-in through P4_KADAN_MATERIAL_CENSUS
// (a label). P4_KADAN_MATERIAL_CENSUS_PREFIX limits it to packages under these comma-separated paths,
// P4_KADAN_MATERIAL_CENSUS_BATCH sets the batch size. A batch whose rows are already saved is not run again,
// so a stopped census goes on where it stopped. Rows and batch reports are kept in Saved/Kadan/MaterialCensus.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialCensusBench, "P4MapKit.Kadan.MaterialCensusBench",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialCensusBench::RunTest(const FString&)
{
    using namespace P4MaterialCensus;
    const FString Label = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_MATERIAL_CENSUS"));
    const FString Paks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    const FString Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    const FString Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Label.IsEmpty() || Paks.IsEmpty() || Retoc.IsEmpty() || Repak.IsEmpty())
    {
        AddWarning(TEXT(
            "P4MapKit test not run. Set P4_KADAN_MATERIAL_CENSUS, P4_NATIVE_ORIGINAL_CONTAINERS, P4_NATIVE_RETOC and P4_NATIVE_REPAK."));
        return true;
    }
    TArray<FString> Prefixes;
    FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_MATERIAL_CENSUS_PREFIX"))
        .ParseIntoArray(Prefixes, TEXT(","), true);
    const int32 BatchSize = FMath::Max(
        1, FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_MATERIAL_CENSUS_BATCH"))));
    const int32 Batch = BatchSize > 1 ? BatchSize : 300;
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("Kadan/MaterialCensus") / Label;
    IFileManager::Get().MakeDirectory(*Folder, true);

    FP4KadanSourceCatalog Catalog;
    FString Error;
    if (!TestTrue(
            TEXT("The game's Paks catalog reads. ") + Error, P4KadanReadSourceCatalog(Paks, Retoc, Catalog, Error)))
        return false;
    FP4KadanImportSettings Settings = Catalog.ImportSettings;
    Settings.Repak = Repak;
    // The editor's import: a package that cannot be rebuilt stays the game's, with its reason.
    Settings.AllowGameBackedLevels = true;
    FP4ExportContainerTools Tools;
    Tools.Retoc = Retoc;
    Tools.Repak = Repak;
    Tools.TimeoutSeconds = 600;
    FP4CurrentGameRegistryPtr Registry;
    if (!TestTrue(TEXT("The game's registry reads. ") + Error,
            P4AcquireCurrentGameRegistry(Tools, Settings.GamePaks, Folder / TEXT("Registry"), Registry, Error)))
        return false;
    TArray<FString> Masters, Instances;
    if (!TestTrue(
            TEXT("The registry names its materials. ") + Error, Registry->MaterialPackages(Masters, Instances, Error)))
        return false;
    const auto Selected = [&](const FString& Package)
    {
        if (!Prefixes.Num())
            return true;
        for (const FString& Prefix : Prefixes)
            if (Package.StartsWith(Prefix))
                return true;
        return false;
    };
    TArray<FString> Roots;
    for (const FString& Package : Masters)
        if (Selected(Package))
            Roots.Add(Package);
    const int32 SelectedMasters = Roots.Num();
    for (const FString& Package : Instances)
        if (Selected(Package))
            Roots.Add(Package);
    if (!Roots.Num())
    {
        AddError(
            TEXT("The census selected no material roots; check P4_KADAN_MATERIAL_CENSUS_PREFIX (comma separated)."));
        return false;
    }
    AddInfo(FString::Printf(
        TEXT("Census %s: the registry names %d masters and %d instances; %d masters and %d instances selected."),
        *Label, Masters.Num(), Instances.Num(), SelectedMasters, Roots.Num() - SelectedMasters));
    {
        auto Header = MakeShared<FJsonObject>();
        Header->SetNumberField(TEXT("registry_masters"), Masters.Num());
        Header->SetNumberField(TEXT("registry_instances"), Instances.Num());
        Header->SetNumberField(TEXT("selected_masters"), SelectedMasters);
        Header->SetNumberField(TEXT("selected_instances"), Roots.Num() - SelectedMasters);
        Header->SetBoolField(TEXT("warm_reimport"),
            FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_MATERIAL_CENSUS_REIMPORT")) == TEXT("1"));
        Header->SetNumberField(TEXT("batch"), Batch);
        Header->SetStringField(TEXT("rhi"), GDynamicRHI ? GDynamicRHI->GetName() : TEXT("none"));
        TArray<TSharedPtr<FJsonValue>> Names;
        for (const FString& Package : Roots)
            Names.Add(MakeShared<FJsonValueString>(Package));
        Header->SetArrayField(TEXT("roots"), Names);
        FString Text;
        FJsonSerializer::Serialize(Header, TJsonWriterFactory<>::Create(&Text));
        FFileHelper::SaveStringToFile(
            Text, *(Folder / TEXT("census.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    const bool Resume = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_MATERIAL_CENSUS_REIMPORT")) != TEXT("1");
    const bool Renderer = !GUsingNullRHI;
    TFunction<bool(const TArray<FString>&, const FString&)> Run;
    Run = [&](const TArray<FString>& Names, const FString& Key)
    {
        const FString RowsFile = Folder / (Key + TEXT(".rows.json"));
        if (IFileManager::Get().FileExists(*RowsFile))
            return true;
        TSet<FString> BatchRoots;
        for (const FString& Name : Names)
            BatchRoots.Add(Name);
        const double Began = FPlatformTime::Seconds();
        const FP4KadanImportPlan Plan = P4KadanPlanImport(Settings, BatchRoots, Resume);
        // The planner refuses a whole request with one missing registry root. Isolate that root without
        // discarding any error; valid neighbours still run through the same production planner/import.
        if (Plan.Problems.Num() && Names.Num() > 1)
        {
            const int32 Half = Names.Num() / 2;
            const TArray<FString> Left(Names.GetData(), Half), Right(Names.GetData() + Half, Names.Num() - Half);
            return Run(Left, Key + TEXT("-a")) && Run(Right, Key + TEXT("-b"));
        }
        const FP4KadanImportResult Result = P4KadanRunImport(Settings, Plan, Resume);
        FFileHelper::SaveStringToFile(Result.Report, *(Folder / (Key + TEXT(".import.json"))),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        if (Result.Cancelled)
        {
            AddError(TEXT("The census import was cancelled."));
            return false;
        }
        TSharedPtr<FJsonObject> Report;
        const TSharedPtr<FJsonObject>* Assets = nullptr;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.Report), Report) && Report.IsValid())
            Report->TryGetObjectField(TEXT("assets"), Assets);
        TSet<FString> Packages = BatchRoots;
        if (Assets)
            for (const auto& Pair : (*Assets)->Values)
            {
                const TSharedPtr<FJsonObject>* Reply = nullptr;
                FString Kind;
                if (Pair.Value->TryGetObject(Reply) && (*Reply)->TryGetStringField(TEXT("kind"), Kind) &&
                    (Kind == TEXT("Material") || Kind == TEXT("MaterialInstanceConstant")))
                    Packages.Add(Pair.Key);
            }
        if (Renderer && GShaderCompilingManager)
            GShaderCompilingManager->FinishAllCompilation();
        TArray<TSharedPtr<FJsonValue>> Rows;
        for (const FString& Package : Packages)
        {
            const TSharedPtr<FJsonObject>* Reply = nullptr;
            const bool Has = Assets && (*Assets)->TryGetObjectField(Package, Reply);
            auto One = Row(Package, Has ? *Reply : TSharedPtr<FJsonObject>(), Result);
            if (!Has)
                One->SetStringField(TEXT("reason"),
                    Plan.Problems.Num() ? FString::Join(Plan.Problems, TEXT("; "))
                                        : TEXT("The import did not report this package."));
            One->SetBoolField(TEXT("root"), BatchRoots.Contains(Package));
            One->SetBoolField(TEXT("reported"), Has);
            if (Renderer && One->GetStringField(TEXT("state")) == TEXT("editable"))
            {
                const FString Why = CompileError(Package);
                if (!Why.IsEmpty())
                    One->SetStringField(TEXT("compile_error"), Why);
            }
            Rows.Add(MakeShared<FJsonValueObject>(One));
        }
        auto Saved = MakeShared<FJsonObject>();
        Saved->SetArrayField(TEXT("rows"), Rows);
        Saved->SetNumberField(TEXT("seconds"), FPlatformTime::Seconds() - Began);
        Saved->SetNumberField(TEXT("plan_assets"), Plan.Assets.Num());
        Saved->SetNumberField(TEXT("plan_problems"), Plan.Problems.Num());
        TArray<TSharedPtr<FJsonValue>> PlanProblems;
        for (const FString& Problem : Plan.Problems)
            PlanProblems.Add(MakeShared<FJsonValueString>(Problem));
        Saved->SetArrayField(TEXT("plan_problem_text"), PlanProblems);
        FString Text;
        FJsonSerializer::Serialize(Saved, TJsonWriterFactory<>::Create(&Text));
        FFileHelper::SaveStringToFile(Text, *RowsFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        AddInfo(FString::Printf(TEXT("Census %s: %d roots, %d material rows, %.0f s."), *Key, BatchRoots.Num(),
            Rows.Num(), FPlatformTime::Seconds() - Began));
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        return true;
    };
    const int32 FirstBatch = FMath::Max(
        0, FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_MATERIAL_CENSUS_FIRST_BATCH"))));
    const int32 MaxBatches = FMath::Max(
        0, FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_MATERIAL_CENSUS_MAX_BATCHES"))));
    for (int32 Start = 0, Index = 0; Start < Roots.Num(); Start += Batch, ++Index)
    {
        if (Index < FirstBatch)
            continue;
        if (MaxBatches && Index - FirstBatch >= MaxBatches)
            break;
        TArray<FString> Names;
        for (int32 I = Start; I < FMath::Min(Roots.Num(), Start + Batch); ++I)
            Names.Add(Roots[I]);
        if (!Run(Names, FString::Printf(TEXT("batch-%03d"), Index)))
            return false;
    }
    return true;
}
#endif

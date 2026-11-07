#include "P4KadanReimportAsset.h"
#include "P4GamePaths.h"
#include "P4CreatorSource.h"
#include "P4OriginalRecord.h"
#include "P4MaterialGraph.h"
#include "P4MaterialInstance.h"
#include "P4MaterialInstanceComparison.h"
#if P4_KADAN_TEST_SEAMS
#include "P4MaterialAdmissionWitness.h"
#endif
#include "P4KadanImportSources.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "P4NativeHash.h"
#include "P4LevelContent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "AssetRegistryModule.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "Editor.h"
#include "UObject/UObjectIterator.h"
#include "PackageTools.h"
#include "Misc/PackageName.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
TSet<FString> ReimportSlots;
int32 SavedLevelScopes = 0;
TSet<FName> PendingPackagesBeforeReimport;
FString State(UObject* Asset, FString& Error, FP4MaterialInstanceNativeEvidence* Evidence = nullptr,
    TArray<FString>* NativeLines = nullptr)
{
    if (UMaterial* Material = Cast<UMaterial>(Asset))
        return P4MaterialComparisonState(Material, Error);
    if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset))
    {
        FP4OriginalRecord Record;
        FString ReadError;
        if (P4ReadOriginalRecord(Instance, Record, ReadError) &&
            Record.ComparisonSchema == TEXT("material-instance-native-state-v1"))
            return P4MaterialInstanceComparisonState(Instance, Error, Evidence, NativeLines);
        const FString Json = P4MaterialInstanceJson(Instance, Error);
        return Error.IsEmpty() && !Json.IsEmpty() ? Json : FString();
    }
    const auto Diagnostic = P4DiagnoseOriginalAsset(Asset);
    if (Diagnostic.Error == TEXT("original-comparison-schema-not-supported"))
    {
        // New read-only identity/reference families have their own source admission. Their
        // complete current serialized editor state still has to survive this native reload.
        const FString Current = P4OriginalObjectAssetState(Asset);
        if (!Current.IsEmpty())
            return Current;
    }
    Error = Diagnostic.Error;
    return Diagnostic.CurrentDigest;
}
} //namespace
bool P4KadanOwnsReimportSlot(const FString& Package)
{
    return IsInGameThread() && ReimportSlots.Contains(Package);
}

FP4KadanReimportAsset::FP4KadanReimportAsset(const FString& Package) : Name(Package)
{
    if (!IsInGameThread() || ReimportSlots.Contains(Name))
    {
        Error = TEXT("reimport-package-already-in-use");
        return;
    }
    if (!FPackageName::DoesPackageExist(Name))
        return;
    Path = P4ObjectPathOf(Name);
    Original.Reset(LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet));
    if (!Original.IsValid())
    {
        Error = TEXT("reimport-existing-package-unreadable: ") + Name;
        return;
    }
    UPackage* PackageObject = Original->GetOutermost();
    FP4OriginalRecord Record;
    FString Why;
    const auto Witness = [&](const TCHAR* Cause, const FP4OriginalDiagnostic* Diagnostic, UObject* ExtraRoot)
    {
#if P4_KADAN_TEST_SEAMS
        if (!P4MICAdmissionWitness::Enabled(Name))
            return;
        auto Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("preserve_cause"), Cause);
        Out->SetStringField(TEXT("record_error"), Why);
        Out->SetBoolField(TEXT("record_evaluated"), FString(Cause) != TEXT("creator-source"));
        Out->SetBoolField(TEXT("package_dirty_at_decision"), PackageObject->IsDirty());
        Out->SetNumberField(TEXT("saved_level_scopes"), SavedLevelScopes);
        Out->SetBoolField(TEXT("pending_dirty_at_scope_entry"), PendingPackagesBeforeReimport.Contains(FName(*Name)));
        Out->SetBoolField(TEXT("diagnostic_evaluated"), Diagnostic != nullptr);
        if (ExtraRoot)
            Out->SetStringField(TEXT("blocking_root"), ExtraRoot->GetPathName());
        if (Diagnostic)
        {
            const TCHAR* Compared = Diagnostic->Comparison == EP4OriginalComparison::Equal ? TEXT("Equal")
                : Diagnostic->Comparison == EP4OriginalComparison::Different               ? TEXT("Different")
                : Diagnostic->Comparison == EP4OriginalComparison::Unreadable              ? TEXT("Unreadable")
                                                                                           : TEXT("NotCompared");
            Out->SetStringField(TEXT("actual_comparison"), Compared);
            Out->SetStringField(TEXT("actual_diagnostic_error"), Diagnostic->Error);
            Out->SetStringField(TEXT("actual_current_digest"), Diagnostic->CurrentDigest);
            Out->SetStringField(TEXT("expected_digest"), Diagnostic->Provenance.Record.ComparisonDigest);
        }
        const FString Phase = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_ADMISSION_WITNESS_PHASE"));
        if (!P4MICAdmissionWitness::Capture(Original.Get(), TEXT("admission-") + Phase, Out))
            UE_LOG(LogTemp, Warning, TEXT("Exact MIC admission witness could not be written for %s"), *Name);
#endif
    };
    // Copies, replacements, unproven imports and edited originals keep their own contents.
    // Source discovery has already used the game's actual package, so retaining content here
    // does not prevent the normal pipeline from finding new references.
    const bool CreatorSource = P4HasCreatorSource(Original.Get());
    if (CreatorSource || !P4ReadOriginalRecord(Original.Get(), Record, Why))
    {
        KeepExisting = true;
        Witness(CreatorSource ? TEXT("creator-source") : TEXT("record-unreadable"), nullptr, nullptr);
        return;
    }
    TArray<UObject*> Roots;
    GetObjectsWithOuter(PackageObject, Roots, false);
    for (UObject* Root : Roots)
        if (IsValid(Root) && Root != Original.Get() && !Root->IsA<UMetaData>())
        {
            KeepExisting = true;
            Witness(TEXT("extra-package-root"), nullptr, Root);
            return;
        }
    const auto Diagnostic = P4DiagnoseOriginalAsset(Original.Get());
    const bool DirtyRefusal = PackageObject->IsDirty() &&
        (!SavedLevelScopes || PendingPackagesBeforeReimport.Contains(FName(*Name)));
    if (DirtyRefusal || (Diagnostic.Comparison != EP4OriginalComparison::Equal && !P4IsVerifiedStandIn(Original.Get())))
    {
        KeepExisting = true;
        Witness(DirtyRefusal ? TEXT("dirty-package") : TEXT("original-comparison"), &Diagnostic, nullptr);
        return;
    }
    OriginalFlags = PackageObject->GetPackageFlags();
    PreviousPackage.Reset(PackageObject);
    const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Retired = TEXT("/Temp/KadanAssetReimport_") + Token + TEXT("/") + Original->GetName();
    Stage = Retired + TEXT("_Stage");
    ResetLoaders(PackageObject);
    if (!PackageObject->Rename(*Retired, nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional))
    {
        Error = TEXT("reimport-could-not-stage-original-package: ") + Name;
        return;
    }
    Active = true;
    ReimportSlots.Add(Name);
}

bool FP4KadanReimportAsset::Restore()
{
    if (!Active)
        return true;
    if (UPackage* Fresh = FindPackage(nullptr, *Name))
    {
        ResetLoaders(Fresh);
        if (!Fresh->Rename(*Stage, nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional))
            return false;
        TArray<UObject*> Objects;
        GetObjectsWithOuter(Fresh, Objects, true);
        for (UObject* Object : Objects)
            Object->ClearFlags(RF_Public | RF_Standalone);
        Fresh->SetDirtyFlag(false);
    }
    if (!PreviousPackage->Rename(*Name, nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional))
        return false;
    Active = false;
    ReimportSlots.Remove(Name);
    FAssetRegistryModule::AssetCreated(Original.Get());
    // AssetCreated sets PKG_NewlyCreated, which is part of PKG_InMemoryOnly. This is
    // an existing saved package; restore its flags after refreshing the registry row.
    PreviousPackage->SetPackageFlagsTo(OriginalFlags);
    return true;
}
FP4KadanReimportAsset::~FP4KadanReimportAsset()
{
    if (Active)
        ensure(Restore());
}

bool FP4KadanReimportAsset::Finish(FString& Why)
{
    if (!Active)
        return true;
    UObject* Fresh = FindObject<UObject>(nullptr, *Path);
    if (!IsValid(Fresh))
    {
        Why = TEXT("reimport-made-no-asset: ") + Name;
        return false;
    }
    // For strict MICs, trace components and lines come from the same cook already
    // used by State; tracing must not add an observational recook to that predicate.
    const bool Trace = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_REIMPORT_TRACE")) == TEXT("1");
    FP4MaterialInstanceNativeEvidence ExpectedMIC, ActualMIC;
    TArray<FString> ExpectedLines, ActualLines;
    const FString Expected = State(Fresh, Why, Trace ? &ExpectedMIC : nullptr, Trace ? &ExpectedLines : nullptr);
    if (Expected.IsEmpty() || !Why.IsEmpty())
        return false;
    if (Trace && ExpectedMIC.NativeDigest.IsEmpty())
        P4OriginalObjectAssetStateAndLines(Fresh, ExpectedLines);
    if (!Restore())
    {
        Why = TEXT("reimport-could-not-restore-package-identity: ") + Name;
        return false;
    }
    UPackage* Old = PreviousPackage.Get();
    Old->SetDirtyFlag(false);
    Original.Reset();
    PreviousPackage.Reset();
    FText ReloadError;
    if (!UPackageTools::ReloadPackages({Old}, ReloadError, EReloadPackagesInteractionMode::AssumePositive))
    {
        Why = TEXT("reimport-reload-failed: ") + ReloadError.ToString();
        return false;
    }
    UObject* Reloaded = LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    FString ReadError;
    const FString Actual = Reloaded
        ? State(Reloaded, ReadError, Trace ? &ActualMIC : nullptr, Trace ? &ActualLines : nullptr)
        : FString();
    if (Trace && (!ExpectedMIC.NativeDigest.IsEmpty() || !ActualMIC.NativeDigest.IsEmpty()))
    {
        const auto Components =
            [&](const TCHAR* Phase, const FString& Composite, const FP4MaterialInstanceNativeEvidence& Item)
        {
            UE_LOG(LogTemp, Display,
                TEXT(
                    "MIC native components %s %s: schema=material-instance-native-state-v1 composite=%s native=%s transport-present=%d transport-length=%d transport=%s custody-present=%d custody-length=%d custody=%s source=%s recipe=%s"),
                Phase, *Name, *Composite, *Item.NativeDigest, Item.HasTransport, Item.TransportLength,
                *Item.TransportDigest, Item.HasCustody, Item.CustodyLength, *Item.CustodyDigest, *Item.SourceSha256,
                *Item.RecipeMd5);
        };
        Components(TEXT("expected"), Expected, ExpectedMIC);
        Components(TEXT("actual"), Actual, ActualMIC);
    }
    if (!Reloaded || Actual != Expected || !ReadError.IsEmpty())
    {
        if (Trace && Reloaded)
        {
            if (ActualMIC.NativeDigest.IsEmpty())
                P4OriginalObjectAssetStateAndLines(Reloaded, ActualLines);
            UE_LOG(LogTemp, Display,
                TEXT("Reimport cold state %s: expected=%s actual=%s expected-lines=%d actual-lines=%d"), *Name,
                *Expected, *Actual, ExpectedLines.Num(), ActualLines.Num());
            int32 Removed = 0, Added = 0;
            for (const FString& Line : ExpectedLines)
                if (!ActualLines.Contains(Line) && Removed++ < 4)
                    UE_LOG(LogTemp, Display, TEXT("Reimport cold removed: %s"), *Line.Left(4096));
            for (const FString& Line : ActualLines)
                if (!ExpectedLines.Contains(Line) && Added++ < 4)
                    UE_LOG(LogTemp, Display, TEXT("Reimport cold added: %s"), *Line.Left(4096));
        }
        Why = TEXT("reimport-saved-content-differs: ") + Name + TEXT(" ") + ReadError;
        return false;
    }
    return true;
}

FP4KadanReimportScope::FP4KadanReimportScope(bool Enabled, const TSet<FString>& Worlds)
{
    if (!Enabled)
        return;
    if (GEditor && GEditor->PlayWorld)
    {
        Error = TEXT("Finish Play before reimporting.");
        return;
    }
    if (!SavedLevelScopes)
    {
        PendingPackagesBeforeReimport.Empty();
        for (TObjectIterator<UPackage> It; It; ++It)
            if (It->IsDirty())
                PendingPackagesBeforeReimport.Add(It->GetFName());
    }
    if (!SavedLevelScopes)
        for (TObjectIterator<UWorld> It; It; ++It)
        {
            UPackage* Package = It->GetOutermost();
            if (IsValid(*It) && P4IsContentPackageIgnoringCase(Package->GetName()) && Package->IsDirty())
            {
                Error = TEXT("Save your level changes before reimporting.");
                return;
            }
        }
    // Load every selected saved world now, including worlds the browser has not opened. Capturing
    // only worlds already loaded would mistake later dependency changes for saved creator work.
    TArray<FString> Pending = Worlds.Array();
    TSet<FName> Seen;
    while (Pending.Num())
    {
        const FString Package = Pending.Pop(false);
        if (Package.IsEmpty())
            continue;
        if (Seen.Contains(FName(*Package)))
            continue;
        if (Seen.Num() >= 4096)
        {
            Error = TEXT("level-reimport-saved-streaming-graph-exceeds-limit");
            if (!SavedLevelScopes)
            {
                PendingPackagesBeforeReimport.Empty();
                P4ClearSavedLevelReimports();
            }
            return;
        }
        Seen.Add(FName(*Package));
        if (!FPackageName::DoesPackageExist(Package))
            continue;
        UWorld* World = LoadObject<UWorld>(nullptr, *P4ObjectPathOf(Package), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!World || !P4CaptureSavedLevelReimport(World, Error))
        {
            if (Error.IsEmpty())
                Error = TEXT("level-reimport-snapshot-could-not-load: ") + Package;
            if (!SavedLevelScopes)
            {
                PendingPackagesBeforeReimport.Empty();
                P4ClearSavedLevelReimports();
            }
            return;
        }
        // Container-backed discovery adds retained sublevels after the initial plan.
        // Their saved creator state must be captured with the root, before any shared
        // Blueprint dependency can reinstance actors in a later-loaded child.
        for (ULevelStreaming* Stream : World->GetStreamingLevels())
            if (Stream)
                Pending.Add(Stream->GetWorldAssetPackageName());
    }
    ++SavedLevelScopes;
    Active = true;
}
FP4KadanReimportScope::~FP4KadanReimportScope()
{
    if (Active && --SavedLevelScopes == 0)
    {
        PendingPackagesBeforeReimport.Empty();
        P4ClearSavedLevelReimports();
    }
}
bool P4KadanHasSavedLevelReimportScope()
{
    return IsInGameThread() && SavedLevelScopes > 0;
}

#if P4_KADAN_TEST_SEAMS
void* P4BeginSavedLevelReimportForTest(const FString& Package, FString& Error)
{
    auto* Scope = new FP4KadanReimportScope(true, TSet<FString>{Package});
    Error = Scope->Error;
    if (!Error.IsEmpty())
    {
        delete Scope;
        return nullptr;
    }
    return Scope;
}
void P4EndSavedLevelReimportForTest(void* Scope)
{
    delete static_cast<FP4KadanReimportScope*>(Scope);
}
#endif

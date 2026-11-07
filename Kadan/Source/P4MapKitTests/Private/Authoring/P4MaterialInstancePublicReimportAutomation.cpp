#if WITH_DEV_AUTOMATION_TESTS
#include "P4KadanImport.h"
#include "P4KadanSourceCatalog.h"
#include "P4KadanImportSources.h"
#include "P4CreatorSource.h"
#include "P4MaterialInstance.h"
#include "P4MaterialInstanceComparison.h"
#include "P4OriginalImportSave.h"
#include "P4MaterialGraph.h"
#include "P4MaterialDependencyRetention.h"
#include "P4MaterialAdmissionWitness.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Materials/MaterialParameterCollection.h"
#include "MaterialShared.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "RHI.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShaderCompiler.h"
#include "StaticParameterSet.h"
#include "AssetRegistryModule.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/PackageReload.h"

namespace P4MIPublicReimport
{
const FString Package = TEXT("/Game/Carrot4/Effects/Cmn/Materials/MI_eft_Cmn_Awa00_alcfr");
const FString Parent = TEXT("/Game/Carrot4/MasterMaterial/eft/M_eft_Water01_alcfr_Master");
const FString Collection = TEXT("/Game/Carrot4/MasterMaterial/ParameterCollections/MPC_Camera");
FString Full(FString Path)
{
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Path);
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}
bool Bundle(TMap<FString, FString>& Out, const FString& Identity = Package)
{
    Out.Reset();
    const FString Stem = FPackageName::LongPackageNameToFilename(Identity);
    for (const TCHAR* Extension : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")})
    {
        const FString File = Stem + Extension;
        FString Hash = TEXT("absent");
        int64 Size = 0;
        if (IFileManager::Get().DirectoryExists(*File) ||
            (IFileManager::Get().FileExists(*File) && !P4HashFile(File, 64ll * 1024 * 1024, Size, Hash)))
            return false;
        Out.Add(Extension, Hash);
    }
    return Out.FindRef(TEXT(".uasset")) != TEXT("absent");
}
bool Same(const TMap<FString, FString>& A, const TMap<FString, FString>& B)
{
    if (A.Num() != B.Num())
        return false;
    for (const auto& X : A)
    {
        const auto* Y = B.Find(X.Key);
        if (!Y || !P4ExactNativeString(X.Value, *Y))
            return false;
    }
    return true;
}
TSharedRef<FJsonObject> Json(const TMap<FString, FString>& Files)
{
    auto Out = MakeShared<FJsonObject>();
    for (const auto& X : Files)
        Out->SetStringField(X.Key, X.Value);
    return Out;
}
bool Save(const FString& File, const TSharedRef<FJsonObject>& Value)
{
    FString Text;
    return FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Text)) &&
        FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
bool Flag(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
{
    bool Out = false;
    return Object.IsValid() && Object->TryGetBoolField(Name, Out) && Out;
}
FString Text(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
{
    FString Out;
    if (Object.IsValid())
        Object->TryGetStringField(Name, Out);
    return Out;
}
TSharedPtr<FJsonObject> Reply(const FP4KadanImportResult& Result, const FString& Identity = Package)
{
    TSharedPtr<FJsonObject> Report;
    const TSharedPtr<FJsonObject>*Assets = nullptr, *Row = nullptr;
    if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.Report), Report) && Report.IsValid() &&
        Report->TryGetObjectField(TEXT("assets"), Assets) && (*Assets)->TryGetObjectField(Identity, Row))
        return *Row;
    return nullptr;
}
bool Witness(const TSharedPtr<FJsonObject>& Row)
{
    const TSharedPtr<FJsonObject>* Family = nullptr;
    return Row.IsValid() && Row->TryGetObjectField(TEXT("material_family_sources"), Family) &&
        Flag(*Family, TEXT("original_binding_parent_recipe_recovered")) &&
        Flag(*Family, TEXT("original_binding_witness_read_only")) &&
        P4ExactNativeString(
            Text(*Family, TEXT("original_binding_parent_recipe_md5")), TEXT("afae0ef4555fdd5c757200d82cc1800f"));
}

FString ParameterBindingFrame(const UMaterialInstanceConstant* Instance)
{
    TArray<FString> Lines;
    const auto Add = [&](const TCHAR* Group, const auto& Rows)
    {
        for (const auto& Row : Rows)
            Lines.Add(FString::Printf(TEXT("%s | %s | %d | %d | %s"), Group, *Row.ParameterInfo.Name.ToString(),
                int32(Row.ParameterInfo.Association), Row.ParameterInfo.Index,
                *Row.ExpressionGUID.ToString(EGuidFormats::Digits)));
    };
    Add(TEXT("scalar"), Instance->ScalarParameterValues);
    Add(TEXT("vector"), Instance->VectorParameterValues);
    Add(TEXT("texture"), Instance->TextureParameterValues);
    Add(TEXT("switch"), Instance->GetStaticParameters().StaticSwitchParameters);
    return FString::Join(Lines, TEXT("\n"));
}

bool FreshNativeControls(
    FAutomationTestBase& Test, UMaterialInstanceConstant* Instance, const TSharedRef<FJsonObject>& Proof)
{
    if (!Instance || !Instance->ScalarParameterValues.Num() ||
        !Instance->GetStaticParameters().StaticSwitchParameters.Num())
        return false;
    P4ImportPublication::FImportScope TestEdits;
    UMetaData* Meta = Instance->GetOutermost()->GetMetaData();
    FString Error;
    const FString Marker = Meta->GetValue(Instance, TEXT("P4OriginalRecord"));
    const FString Original = P4MaterialInstanceComparisonState(Instance, Error);
    if (!Test.TestTrue(TEXT("Fresh strict native baseline is readable"), Error.IsEmpty() && !Original.IsEmpty()))
        return false;
    auto Controls = MakeShared<FJsonObject>();
    const auto Different = [&](const TCHAR* Name, const TFunction<void()>& Edit, const TFunction<void()>& Restore)
    {
        const bool Dirty = Instance->GetOutermost()->IsDirty();
        Edit();
        ON_SCOPE_EXIT
        {
            Restore();
            Instance->GetOutermost()->SetDirtyFlag(Dirty);
        };
        const auto Diagnostic = P4DiagnoseOriginalAsset(Instance);
        const bool Refused = Diagnostic.Comparison != EP4OriginalComparison::Equal;
        Controls->SetBoolField(Name, Refused);
        return Test.TestTrue(FString(TEXT("Fresh MIC strict comparison refuses ")) + Name, Refused);
    };
    const auto Restored = [&]()
    {
        FString Why;
        const auto Diagnostic = P4DiagnoseOriginalAsset(Instance);
        return Diagnostic.Error.IsEmpty() && Diagnostic.Comparison == EP4OriginalComparison::Equal &&
            P4ExactNativeString(P4MaterialInstanceComparisonState(Instance, Why), Original) && Why.IsEmpty() &&
            P4ExactNativeString(Meta->GetValue(Instance, TEXT("P4OriginalRecord")), Marker);
    };
    const FScalarParameterValue Scalar = Instance->ScalarParameterValues[0];
    const auto RestoreScalar = [&]() { Instance->ScalarParameterValues[0] = Scalar; };
    if (!Different(
            TEXT("scalar-value"), [&]()
            { Instance->ScalarParameterValues[0].ParameterValue = Scalar.ParameterValue + 0.125f; }, RestoreScalar) ||
        !Restored())
        return false;
    if (!Different(
            TEXT("scalar-expression-guid"),
            [&]() { Instance->ScalarParameterValues[0].ExpressionGUID = FGuid::NewGuid(); }, RestoreScalar) ||
        !Restored())
        return false;
    if (!Different(
            TEXT("scalar-name"),
            [&]() { Instance->ScalarParameterValues[0].ParameterInfo.Name = FName(TEXT("P4ChangedNativeScalarName")); },
            RestoreScalar) ||
        !Restored())
        return false;
    if (!Different(
            TEXT("scalar-layer-index"), [&]() { Instance->ScalarParameterValues[0].ParameterInfo.Index = 1; },
            RestoreScalar) ||
        !Restored())
        return false;
    auto& Static = const_cast<FStaticParameterSet&>(Instance->GetStaticParameters());
    const FStaticSwitchParameter Switch = Static.StaticSwitchParameters[0];
    if (!Different(
            TEXT("static-switch-value"), [&]() { Static.StaticSwitchParameters[0].Value = !Switch.Value; },
            [&]() { Static.StaticSwitchParameters[0] = Switch; }) ||
        !Restored())
        return false;
    const auto Base = Instance->BasePropertyOverrides;
    if (!Different(
            TEXT("native-base-override"),
            [&]() { Instance->BasePropertyOverrides.bOverride_TwoSided = !Base.bOverride_TwoSided; },
            [&]() { Instance->BasePropertyOverrides = Base; }) ||
        !Restored())
        return false;
    const bool HadTransport = Meta->HasValue(Instance, TEXT("P4MaterialInstanceTransportV2"));
    const FString Transport = Meta->GetValue(Instance, TEXT("P4MaterialInstanceTransportV2"));
    const auto RestoreTransport = [&]()
    {
        if (HadTransport)
            Meta->SetValue(Instance, TEXT("P4MaterialInstanceTransportV2"), *Transport);
        else
            Meta->RemoveValue(Instance, TEXT("P4MaterialInstanceTransportV2"));
    };
    if (!Different(
            TEXT("exact-transport-text"),
            [&]() { Meta->SetValue(Instance, TEXT("P4MaterialInstanceTransportV2"), *(TEXT(" ") + Transport)); },
            RestoreTransport) ||
        !Restored())
        return false;
    if (!HadTransport &&
        (!Different(
             TEXT("absent-versus-empty-transport"),
             [&]() { Meta->SetValue(Instance, TEXT("P4MaterialInstanceTransportV2"), TEXT("")); }, RestoreTransport) ||
            !Restored()))
        return false;
    const FString SourceSeal = Meta->GetValue(Instance, TEXT("P4MaterialInstanceNativeSourceV1"));
    TSharedPtr<FJsonObject> Source;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceSeal), Source) || !Source.IsValid())
        return false;
    const auto RestoreSource = [&]()
    { Meta->SetValue(Instance, TEXT("P4MaterialInstanceNativeSourceV1"), *SourceSeal); };
    const auto WriteSource = [&]()
    {
        FString Text;
        FJsonSerializer::Serialize(
            Source.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
        Meta->SetValue(Instance, TEXT("P4MaterialInstanceNativeSourceV1"), *Text);
    };
    if (!Different(
            TEXT("missing-current-source-seal"),
            [&]() { Meta->RemoveValue(Instance, TEXT("P4MaterialInstanceNativeSourceV1")); }, RestoreSource) ||
        !Restored())
        return false;
    const FString RawSha = Source->GetStringField(TEXT("header_body_sha256"));
    FString ChangedSha = RawSha;
    ChangedSha[0] = ChangedSha[0] == TEXT('0') ? TEXT('1') : TEXT('0');
    Source->SetStringField(TEXT("header_body_sha256"), ChangedSha);
    if (!Different(TEXT("changed-raw-source-sha256"), WriteSource, RestoreSource) || !Restored())
        return false;
    Source->SetStringField(TEXT("header_body_sha256"), RawSha);
    FString ChangedRecipe = Source->GetStringField(TEXT("recipe_md5"));
    ChangedRecipe[0] = ChangedRecipe[0] == TEXT('0') ? TEXT('1') : TEXT('0');
    Source->SetStringField(TEXT("recipe_md5"), ChangedRecipe);
    if (!Different(TEXT("changed-source-recipe-md5"), WriteSource, RestoreSource) || !Restored())
        return false;
    TSharedPtr<FJsonObject> Record;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Marker), Record) || !Record.IsValid())
        return false;
    const auto WriteRecord = [&]()
    {
        FString Text;
        FJsonSerializer::Serialize(
            Record.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
        Meta->SetValue(Instance, TEXT("P4OriginalRecord"), *Text);
    };
    const auto RestoreRecord = [&]() { Meta->SetValue(Instance, TEXT("P4OriginalRecord"), *Marker); };
    Record->SetStringField(TEXT("comparison_schema"), TEXT("material-instance-v2"));
    if (!Different(TEXT("old-v2-schema-is-still-unsupported"), WriteRecord, RestoreRecord) || !Restored())
        return false;
    Record->SetStringField(TEXT("comparison_schema"), TEXT("material-instance-native-state-v1"));
    Record->SetStringField(TEXT("comparison_digest"), TEXT(""));
    if (!Different(TEXT("missing-fresh-native-baseline"), WriteRecord, RestoreRecord) || !Restored())
        return false;
    FString ChangedDigest = Original;
    ChangedDigest[0] = ChangedDigest[0] == TEXT('0') ? TEXT('1') : TEXT('0');
    Record->SetStringField(TEXT("comparison_digest"), ChangedDigest);
    WriteRecord();
    const auto SelfCheck = P4DiagnoseOriginalAsset(Instance);
    RestoreRecord();
    if (!Test.TestTrue(
            TEXT(
                "Native state excludes its own stored OriginalRecord digest without granting a changed baseline Equal"),
            SelfCheck.Error.IsEmpty() && SelfCheck.Comparison == EP4OriginalComparison::Different &&
                P4ExactNativeString(SelfCheck.CurrentDigest, Original) && Restored()))
        return false;
    Controls->SetBoolField(TEXT("stored-record-digest-does-not-recurse"), true);
    const FString PublicPackage = Package + TEXT("_UnprovenJsonControl");
    TArray<FString> Missing;
    FString PublicError;
    auto* Public = P4BuildMaterialInstance(
        P4MaterialInstanceJson(Instance, PublicError), PublicPackage, Missing, PublicError);
    ON_SCOPE_EXIT
    {
        if (Public)
        {
            FAssetRegistryModule::AssetDeleted(Public);
            Public->ClearFlags(RF_Public | RF_Standalone);
            Public->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
            Public->MarkPendingKill();
        }
    };
    FP4OriginalRecord PublicRecord;
    const bool NoAuthority = Public && PublicError.IsEmpty() &&
        P4ReadOriginalRecord(Public, PublicRecord, PublicError) &&
        PublicRecord.ComparisonSchema == TEXT("material-instance-v2") && PublicRecord.ComparisonDigest.IsEmpty() &&
        !Public->GetOutermost()->GetMetaData()->HasValue(Public, TEXT("P4MaterialInstanceNativeSourceV1")) &&
        P4DiagnoseOriginalAsset(Public).Error == TEXT("original-comparison-schema-not-supported");
    if (!Test.TestTrue(
            TEXT("Public JSON build inside import scope gains no recovered native baseline authority"), NoAuthority))
        return false;
    Controls->SetBoolField(TEXT("public-json-no-authority"), NoAuthority);
    Proof->SetObjectField(TEXT("fresh_native_negative_controls"), Controls);
    Proof->SetBoolField(TEXT("fresh_native_negative_controls_passed"), true);
    return Restored();
}

struct FCollectionCustody
{
    TWeakObjectPtr<UMaterialParameterCollection> Object;
    TWeakObjectPtr<UMaterial> ParentObject;
    FString Marker, Baseline, Notes, ParentMarker, ParentState;
    bool HadNotes = false;
    TMap<FString, FString> Files, ParentFiles;
};
bool RetainedCustody(UMaterialInstanceConstant* Instance, const FCollectionCustody& Before)
{
    auto* C = Before.Object.Get();
    auto* P = Before.ParentObject.Get();
    FString Error;
    if (!C || !P || !Instance || Instance->Parent != P || C->GetOutermost()->IsDirty() || P->GetOutermost()->IsDirty())
        return false;
    auto* Meta = C->GetOutermost()->GetMetaData();
    TMap<FString, FString> Files, ParentFiles;
    const FString State = P4MaterialComparisonState(P, Error);
    return Error.IsEmpty() && P4ExactNativeString(State, Before.ParentState) &&
        P4ExactNativeString(
            Before.ParentMarker, P->GetOutermost()->GetMetaData()->GetValue(P, TEXT("P4OriginalRecord"))) &&
        P4ExactNativeString(Before.Marker, Meta->GetValue(C, TEXT("P4OriginalRecord"))) &&
        P4ExactNativeString(Before.Baseline, Meta->GetValue(C, TEXT("P4NativeObjectBaseline"))) &&
        Before.HadNotes == Meta->HasValue(C, TEXT("P4NativeObjectValuesNotHeld")) &&
        P4ExactNativeString(Before.Notes, Meta->GetValue(C, TEXT("P4NativeObjectValuesNotHeld"))) &&
        Bundle(Files, Collection) && Same(Files, Before.Files) && Bundle(ParentFiles, Parent) &&
        Same(ParentFiles, Before.ParentFiles);
}
bool MasterControls(FAutomationTestBase& Test, UMaterialInstanceConstant* Instance, const FString& Out,
    const TSharedRef<FJsonObject>& Proof, const FCollectionCustody& Before)
{
    auto* P = Before.ParentObject.Get();
    if (!P)
        return false;
    FP4KadanSourceAsset Source;
    Source.Package = Parent;
    Source.Kind = TEXT("Material");
    Source.Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_PUBLIC_PARENT_HEADER"));
    Source.Body = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_PUBLIC_PARENT_BODY"));
    if (!Test.TestTrue(TEXT("Existing raw master pair is explicitly supplied"),
            !Source.Header.IsEmpty() && !Source.Body.IsEmpty()))
        return false;
    FP4KadanImportPlan Plan;
    Plan.bMaterialExecutionDeferred = true;
    Plan.RequestedPackages = {Package};
    // This pass-through callback grants no recipe/read witness. These direct
    // calls are negative controls only; the positive must come from ordinary
    // complete-family preparation inside the actual public importer below.
    const TFunction<bool(FString&)> NoAuthority = [](FString& Error)
    {
        Error.Reset();
        return true;
    };
    auto SourceRow = MakeShared<FP4KadanSourceAsset>(Source);
    FP4KadanImportSourceScope Scope({SourceRow});
    const FString Recipe = TEXT("afae0ef4555fdd5c757200d82cc1800f");
    UObject* Existing = nullptr;
    FString Error;
    auto Negative = MakeShared<FJsonObject>();
    bool Rejected = !P4RetainUnchangedRecoveredMaterialDependency(Plan, Source, Recipe, NoAuthority, Existing, Error) &&
        !Existing;
    Negative->SetStringField(TEXT("no_actual_recovery_witness"), Error);
    if (!Test.TestTrue(TEXT("Actual current source bytes and recipe text alone grant no recovered read witness"),
            Rejected && Error.Contains(TEXT("no existing recovered recipe"))))
        return false;
    const bool TwoSided = P->TwoSided;
    P->TwoSided = !TwoSided;
    P->GetOutermost()->SetDirtyFlag(false);
    Rejected = !P4RetainUnchangedRecoveredMaterialDependency(Plan, Source, Recipe, NoAuthority, Existing, Error) &&
        !Existing;
    P->TwoSided = TwoSided;
    P->GetOutermost()->SetDirtyFlag(false);
    Negative->SetStringField(TEXT("edited_clean_native"), Error);
    if (!Test.TestTrue(TEXT("A clean parent native input edit is refused before any missing-witness fallback"),
            Rejected && Error == TEXT("Recovered material dependency current native content differs")))
        return false;
    Rejected = !P4RetainUnchangedRecoveredMaterialDependency(
                   Plan, Source, TEXT("00000000000000000000000000000000"), NoAuthority, Existing, Error) &&
        !Existing;
    Negative->SetStringField(TEXT("changed_recipe"), Error);
    if (!Test.TestTrue(TEXT("A different incoming recipe cannot retain the parent"),
            Rejected && Error == TEXT("Recovered material dependency incoming recipe differs")))
        return false;
    const FString ChangedFolder = Out / TEXT("changed-master-source");
    TArray<uint8> Header, Body;
    if (!Test.TestTrue(TEXT("Changed master source is a bounded two-file copy in the fresh receipt"),
            IFileManager::Get().FileSize(*Source.Header) > 0 &&
                IFileManager::Get().FileSize(*Source.Header) <= 1024 * 1024 &&
                IFileManager::Get().FileSize(*Source.Body) > 0 &&
                IFileManager::Get().FileSize(*Source.Body) <= 1024 * 1024 &&
                !IFileManager::Get().DirectoryExists(*ChangedFolder) &&
                IFileManager::Get().MakeDirectory(*ChangedFolder, true) &&
                FFileHelper::LoadFileToArray(Header, *Source.Header) &&
                FFileHelper::LoadFileToArray(Body, *Source.Body) && Body.Num() > 0))
        return false;
    Body.Last() ^= 1;
    auto Changed = Source;
    Changed.Header = ChangedFolder / TEXT("M_eft_Water01_alcfr_Master.uasset");
    Changed.Body = FPaths::ChangeExtension(Changed.Header, TEXT("uexp"));
    if (!Test.TestTrue(TEXT("Private changed master pair is written"),
            FFileHelper::SaveArrayToFile(Header, *Changed.Header) && FFileHelper::SaveArrayToFile(Body, *Changed.Body)))
        return false;
    Rejected = !P4RetainUnchangedRecoveredMaterialDependency(Plan, Changed, Recipe, NoAuthority, Existing, Error) &&
        !Existing;
    Negative->SetStringField(TEXT("changed_same_identity_raw"), Error);
    if (!Test.TestTrue(TEXT("Different prepared raw bytes cannot borrow the family's binding"),
            Rejected && Error == TEXT("Recovered material dependency prepared source differs from family")))
        return false;
    auto* Meta = P->GetOutermost()->GetMetaData();
    TSharedPtr<FJsonObject> Altered;
    FString Text;
    if (!Test.TestTrue(TEXT("Master custody control starts from the literal marker"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Before.ParentMarker), Altered) &&
                Altered.IsValid()))
        return false;
    Altered->SetBoolField(TEXT("read_only"), false);
    if (!Test.TestTrue(TEXT("Master custody control serializes"),
            FJsonSerializer::Serialize(Altered.ToSharedRef(), TJsonWriterFactory<>::Create(&Text))))
        return false;
    Meta->SetValue(P, TEXT("P4OriginalRecord"), *Text);
    P->GetOutermost()->SetDirtyFlag(false);
    Rejected = !P4RetainUnchangedRecoveredMaterialDependency(Plan, Source, Recipe, NoAuthority, Existing, Error) &&
        !Existing;
    Meta->SetValue(P, TEXT("P4OriginalRecord"), *Before.ParentMarker);
    P->GetOutermost()->SetDirtyFlag(false);
    Negative->SetStringField(TEXT("incomplete_custody"), Error);
    if (!Test.TestTrue(TEXT("A master without the original read-only policy cannot qualify"),
            Rejected && Error == TEXT("Recovered material dependency original record is incomplete")))
        return false;
    auto Requested = Plan;
    Requested.RequestedPackages = {Parent};
    if (!Test.TestTrue(TEXT("An explicit master root keeps ordinary reimport"),
            !P4RetainUnchangedRecoveredMaterialDependency(Requested, Source, Recipe, NoAuthority, Existing, Error) &&
                !Existing && Error.IsEmpty()))
        return false;
    Requested.RequestedPackages = {Parent.ToLower()};
    if (!Test.TestTrue(TEXT("A case-varied explicit master root keeps ordinary reimport"),
            !P4RetainUnchangedRecoveredMaterialDependency(Requested, Source, Recipe, NoAuthority, Existing, Error) &&
                !Existing && Error.IsEmpty()))
        return false;
    if (!Test.TestTrue(TEXT("All master negatives restore complete native state, identity, marker and saved bytes"),
            RetainedCustody(Instance, Before)))
        return false;
    Proof->SetObjectField(TEXT("master_negative_refusals"), Negative);
    Proof->SetBoolField(TEXT("master_retention_negative_controls_passed"), true);
    return true;
}
bool CollectionControls(FAutomationTestBase& Test, UMaterialInstanceConstant* Instance, const FString& Out,
    const TSharedRef<FJsonObject>& Proof, FCollectionCustody& Before)
{
    FP4KadanSourceAsset Source;
    Source.Package = Collection;
    Source.Kind = TEXT("MaterialParameterCollection");
    Source.Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_PUBLIC_COLLECTION_HEADER"));
    Source.Body = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_PUBLIC_COLLECTION_BODY"));
    auto* C = LoadObject<UMaterialParameterCollection>(nullptr, *(Collection + TEXT(".MPC_Camera")));
    auto* P = Instance ? Cast<UMaterial>(Instance->Parent) : nullptr;
    if (!Test.TestTrue(TEXT("Actual prepared Camera pair, clean collection and exact recovered parent exist"),
            !Source.Header.IsEmpty() && !Source.Body.IsEmpty() && C && P && !C->GetOutermost()->IsDirty() &&
                !P->GetOutermost()->IsDirty()))
        return false;
    Before.Object = C;
    Before.ParentObject = P;
    auto* Meta = C->GetOutermost()->GetMetaData();
    Before.Marker = Meta->GetValue(C, TEXT("P4OriginalRecord"));
    Before.Baseline = Meta->GetValue(C, TEXT("P4NativeObjectBaseline"));
    Before.HadNotes = Meta->HasValue(C, TEXT("P4NativeObjectValuesNotHeld"));
    Before.Notes = Meta->GetValue(C, TEXT("P4NativeObjectValuesNotHeld"));
    Before.ParentMarker = P->GetOutermost()->GetMetaData()->GetValue(P, TEXT("P4OriginalRecord"));
    FString Error;
    Before.ParentState = P4MaterialComparisonState(P, Error);
    const auto ParentDiagnostic = P4DiagnoseOriginalAsset(P);
    if (!Test.TestTrue(
            TEXT("Parent starts at its complete recorded native state, with saved collection/parent bundles"),
            Error.IsEmpty() && !Before.ParentState.IsEmpty() && ParentDiagnostic.Error.IsEmpty() &&
                ParentDiagnostic.Comparison == EP4OriginalComparison::Equal &&
                P4ExactNativeString(Before.ParentState, ParentDiagnostic.CurrentDigest) &&
                P4ExactNativeString(Before.ParentState, ParentDiagnostic.Provenance.Record.ComparisonDigest) &&
                Bundle(Before.Files, Collection) && Bundle(Before.ParentFiles, Parent)))
        return false;
    Proof->SetStringField(TEXT("retained_parent_native_state"), Before.ParentState);
    Proof->SetStringField(TEXT("retained_parent_record"), Before.ParentMarker);
    Proof->SetStringField(TEXT("retained_collection_record"), Before.Marker);
    Proof->SetObjectField(TEXT("retained_collection_files"), Json(Before.Files));
    FP4KadanImportPlan Plan;
    Plan.bMaterialExecutionDeferred = true;
    Plan.RequestedPackages = {Package};
    UObject* Existing = nullptr;
    Error.Reset();
    const bool Retained = P4RetainUnchangedMaterialCollectionDependency(Plan, Source, Existing, Error);
    Proof->SetStringField(TEXT("collection_initial_retention_error"), Error);
    if (!Test.TestTrue(TEXT("Only the exact unchanged unrequested source collection qualifies"),
            Retained && Existing == C && Error.IsEmpty() && RetainedCustody(Instance, Before)))
        return false;
    auto Negative = MakeShared<FJsonObject>();
    for (const TCHAR* Key : {TEXT("P4KadanCopiedGameAsset"), TEXT("P4BlueprintStandIn")})
    {
        const bool Had = Meta->HasValue(C, Key);
        const FString Previous = Meta->GetValue(C, Key);
        Meta->SetValue(C, Key, TEXT("1"));
        C->GetOutermost()->SetDirtyFlag(false);
        const bool RejectedRole = !P4RetainUnchangedMaterialCollectionDependency(Plan, Source, Existing, Error) &&
            !Existing;
        if (Had)
            Meta->SetValue(C, Key, *Previous);
        else
            Meta->RemoveValue(C, Key);
        C->GetOutermost()->SetDirtyFlag(false);
        Negative->SetStringField(Key, Error);
        if (!Test.TestTrue(FString(Key) + TEXT(" copied/proxy identity cannot qualify"),
                RejectedRole && Error == TEXT("Material collection dependency has no clean original native object") &&
                    Meta->HasValue(C, Key) == Had && P4ExactNativeString(Previous, Meta->GetValue(C, Key))))
            return false;
    }
    // A real clean scalar/vector edit must fail current native comparison. No
    // PostEditChange/save/metadata recapture is used to restore the test value.
    if (!Test.TestTrue(TEXT("Camera has a real parameter for the clean edited-content control"),
            C->ScalarParameters.Num() || C->VectorParameters.Num()))
        return false;
    bool Rejected = false;
    if (C->ScalarParameters.Num())
    {
        const float Value = C->ScalarParameters[0].DefaultValue;
        C->ScalarParameters[0].DefaultValue = Value + 0.125f;
        C->GetOutermost()->SetDirtyFlag(false);
        Rejected = !P4RetainUnchangedMaterialCollectionDependency(Plan, Source, Existing, Error) && !Existing;
        C->ScalarParameters[0].DefaultValue = Value;
        C->GetOutermost()->SetDirtyFlag(false);
    }
    else
    {
        const FLinearColor Value = C->VectorParameters[0].DefaultValue;
        C->VectorParameters[0].DefaultValue.R += 0.125f;
        C->GetOutermost()->SetDirtyFlag(false);
        Rejected = !P4RetainUnchangedMaterialCollectionDependency(Plan, Source, Existing, Error) && !Existing;
        C->VectorParameters[0].DefaultValue = Value;
        C->GetOutermost()->SetDirtyFlag(false);
    }
    Negative->SetStringField(TEXT("edited_clean"), Error);
    if (!Test.TestTrue(TEXT("A clean edited collection cannot be retained as unchanged"),
            Rejected && Error == TEXT("Material collection dependency current native content differs")))
        return false;
    Meta->SetValue(C, TEXT("P4NativeObjectBaseline"), TEXT("{}"));
    C->GetOutermost()->SetDirtyFlag(false);
    Rejected = !P4RetainUnchangedMaterialCollectionDependency(Plan, Source, Existing, Error) && !Existing;
    Meta->SetValue(C, TEXT("P4NativeObjectBaseline"), *Before.Baseline);
    C->GetOutermost()->SetDirtyFlag(false);
    Negative->SetStringField(TEXT("incomplete_baseline"), Error);
    if (!Test.TestTrue(TEXT("An incomplete native baseline cannot qualify"),
            Rejected && Error == TEXT("Material collection dependency native baseline is incomplete")))
        return false;
    Meta->SetValue(C, TEXT("P4NativeObjectValuesNotHeld"), TEXT("control: source field was not held"));
    C->GetOutermost()->SetDirtyFlag(false);
    Rejected = !P4RetainUnchangedMaterialCollectionDependency(Plan, Source, Existing, Error) && !Existing;
    if (Before.HadNotes)
        Meta->SetValue(C, TEXT("P4NativeObjectValuesNotHeld"), *Before.Notes);
    else
        Meta->RemoveValue(C, TEXT("P4NativeObjectValuesNotHeld"));
    C->GetOutermost()->SetDirtyFlag(false);
    Negative->SetStringField(TEXT("unsupported_note"), Error);
    if (!Test.TestTrue(TEXT("Unsupported native source notes cannot qualify"),
            Rejected && Error == TEXT("Material collection dependency has unsupported source notes")))
        return false;
    for (const TCHAR* Field : {TEXT("legacy_key"), TEXT("legacy_evidence")})
    {
        TSharedPtr<FJsonObject> Altered;
        FString Text;
        if (!Test.TestTrue(TEXT("Legacy custody negative starts from the literal saved v2 marker"),
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Before.Marker), Altered) &&
                    Altered.IsValid()))
            return false;
        Altered->SetStringField(Field, TEXT("tampered-custody-control"));
        if (!Test.TestTrue(TEXT("Legacy custody negative serializes"),
                FJsonSerializer::Serialize(Altered.ToSharedRef(), TJsonWriterFactory<>::Create(&Text))))
            return false;
        Meta->SetValue(C, TEXT("P4OriginalRecord"), *Text);
        C->GetOutermost()->SetDirtyFlag(false);
        Rejected = !P4RetainUnchangedMaterialCollectionDependency(Plan, Source, Existing, Error) && !Existing;
        Meta->SetValue(C, TEXT("P4OriginalRecord"), *Before.Marker);
        C->GetOutermost()->SetDirtyFlag(false);
        Negative->SetStringField(Field, Error);
        if (!Test.TestTrue(FString(Field) + TEXT(" tampering cannot qualify"),
                Rejected && Error == TEXT("Material collection dependency legacy evidence differs")))
            return false;
    }
    TSharedPtr<FJsonObject> RefOnly;
    FString RefOnlyText;
    if (!Test.TestTrue(TEXT("Reference-only negative starts from the literal saved native baseline"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Before.Baseline), RefOnly) && RefOnly.IsValid()))
        return false;
    RefOnly->SetNumberField(TEXT("reference_only_bytes"), 1);
    if (!Test.TestTrue(TEXT("Reference-only negative serializes"),
            FJsonSerializer::Serialize(RefOnly.ToSharedRef(), TJsonWriterFactory<>::Create(&RefOnlyText))))
        return false;
    Meta->SetValue(C, TEXT("P4NativeObjectBaseline"), *RefOnlyText);
    C->GetOutermost()->SetDirtyFlag(false);
    Rejected = !P4RetainUnchangedMaterialCollectionDependency(Plan, Source, Existing, Error) && !Existing;
    Meta->SetValue(C, TEXT("P4NativeObjectBaseline"), *Before.Baseline);
    C->GetOutermost()->SetDirtyFlag(false);
    Negative->SetStringField(TEXT("reference_only_baseline"), Error);
    if (!Test.TestTrue(TEXT("A reference-only native baseline cannot qualify"),
            Rejected && Error == TEXT("Material collection dependency native baseline is incomplete")))
        return false;
    const FString ChangedFolder = Out / TEXT("changed-collection-source");
    TArray<uint8> Header, Body;
    if (!Test.TestTrue(TEXT("Changed-source control uses only a bounded private two-file copy"),
            IFileManager::Get().FileSize(*Source.Header) > 0 &&
                IFileManager::Get().FileSize(*Source.Header) <= 1024 * 1024 &&
                IFileManager::Get().FileSize(*Source.Body) > 0 &&
                IFileManager::Get().FileSize(*Source.Body) <= 1024 * 1024 &&
                !IFileManager::Get().DirectoryExists(*ChangedFolder) &&
                IFileManager::Get().MakeDirectory(*ChangedFolder, true) &&
                FFileHelper::LoadFileToArray(Header, *Source.Header) &&
                FFileHelper::LoadFileToArray(Body, *Source.Body) && Body.Num() > 0))
        return false;
    Body.Last() ^= 1;
    auto Changed = Source;
    Changed.Header = ChangedFolder / TEXT("MPC_Camera.uasset");
    Changed.Body = ChangedFolder / TEXT("MPC_Camera.uexp");
    if (!Test.TestTrue(TEXT("Changed source pair is written inside the fresh test receipt only"),
            FFileHelper::SaveArrayToFile(Header, *Changed.Header) && FFileHelper::SaveArrayToFile(Body, *Changed.Body)))
        return false;
    Rejected = !P4RetainUnchangedMaterialCollectionDependency(Plan, Changed, Existing, Error) && !Existing;
    Negative->SetStringField(TEXT("changed_same_identity_source"), Error);
    if (!Test.TestTrue(TEXT("Same-canonical source with changed bytes cannot qualify"),
            Rejected && Error == TEXT("Material collection dependency source SHA differs")))
        return false;
    auto Requested = Plan;
    Requested.RequestedPackages = {Collection};
    if (!Test.TestTrue(TEXT("Explicitly requested collection keeps the ordinary reimport policy"),
            !P4RetainUnchangedMaterialCollectionDependency(Requested, Source, Existing, Error) && !Existing &&
                Error.IsEmpty()))
        return false;
    Requested.RequestedPackages = {Collection.ToLower()};
    if (!Test.TestTrue(TEXT("Case-varied explicit package identity also keeps the ordinary reimport policy"),
            !P4RetainUnchangedMaterialCollectionDependency(Requested, Source, Existing, Error) && !Existing &&
                Error.IsEmpty()))
        return false;
    auto Ordinary = Plan;
    Ordinary.bMaterialExecutionDeferred = false;
    if (!Test.TestTrue(TEXT("Ordinary non-material plan keeps its existing policy"),
            !P4RetainUnchangedMaterialCollectionDependency(Ordinary, Source, Existing, Error) && !Existing &&
                Error.IsEmpty()))
        return false;
    if (!Test.TestTrue(
            TEXT(
                "All negative controls restore exact collection custody and leave the full parent native state unchanged"),
            RetainedCustody(Instance, Before) &&
                P4RetainUnchangedMaterialCollectionDependency(Plan, Source, Existing, Error) && Existing == C &&
                Error.IsEmpty()))
        return false;
    Proof->SetObjectField(TEXT("collection_negative_refusals"), Negative);
    Proof->SetBoolField(TEXT("collection_retention_controls_passed"), true);
    return true;
}
} //namespace P4MIPublicReimport

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialInstancePublicReimportTest, "P4MapKit.Kadan.MaterialInstancePublicReimport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialInstancePublicReimportTest::RunTest(const FString&)
{
    using namespace P4MIPublicReimport;
    // Destructive only in the explicitly released disposable warm fixture: the
    // positive replaces its old original through the public reimport service.
    const FString ExpectedProject = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_PUBLIC_PROJECT"));
    const FString Out = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_PUBLIC_OUT"));
    const FString FixtureName = FPaths::GetCleanFilename(FPaths::GetPath(Full(FPaths::GetProjectFilePath())));
    const bool KnownFixture = P4ExactNativeString(FixtureName, TEXT("water-area003")) ||
        P4ExactNativeString(FixtureName, TEXT("water-area003-current62-control"));
    const FString SourceMode = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_PUBLIC_SOURCE_MODE"));
    const bool ColdMode = P4ExactNativeString(SourceMode, TEXT("cold-current-raw")),
               WarmMode = P4ExactNativeString(SourceMode, TEXT("warm-current-cache"));
    const FString ConditionFolder = FPaths::GetPath(FPaths::GetPath(Full(FPaths::GetProjectFilePath())));
    const bool KnownCondition = SourceMode.IsEmpty() ||
        ((ColdMode || WarmMode) && P4ExactNativeString(FixtureName, TEXT("water-area003-current62-control")) &&
            P4ExactNativeString(FPaths::GetCleanFilename(ConditionFolder), ColdMode ? TEXT("cold2") : TEXT("warm")) &&
            P4ExactNativeString(FPaths::GetCleanFilename(FPaths::GetPath(ConditionFolder)), TEXT("current62-control")));
    if (!TestTrue(TEXT("Explicit owned warm fixture and fresh receipt directory are required"),
            !ExpectedProject.IsEmpty() && !Out.IsEmpty() &&
                Full(ExpectedProject) == Full(FPaths::GetProjectFilePath()) && KnownFixture && KnownCondition &&
                Full(Out).StartsWith(Full(FPaths::ProjectSavedDir())) && !IFileManager::Get().DirectoryExists(*Out) &&
                !IFileManager::Get().FileExists(*Out)))
        return false;
    if (!TestFalse(TEXT("Actual renderer is required"), GUsingNullRHI))
        return false;
    if (!TestTrue(TEXT("Owned receipt directory is created"), IFileManager::Get().MakeDirectory(*Out, true)))
        return false;
    auto Proof = MakeShared<FJsonObject>();
    Proof->SetStringField(TEXT("package"), Package);
    Proof->SetStringField(
        TEXT("source_cache_mode"), SourceMode.IsEmpty() ? TEXT("legacy warm-source fixture") : SourceMode);
    Proof->SetStringField(TEXT("route"),
        TEXT("P4KadanEditor settings-backed PlanImport/RunImport, Resume=false; no manual recovery witness"));
    ON_SCOPE_EXIT
    {
        Proof->SetBoolField(TEXT("passed"), !HasAnyErrors());
        if (!Save(Out / TEXT("proof.json"), Proof))
            AddError(TEXT("Cannot save public MIC reimport proof"));
    };
    TArray<TPair<FString, FString>> Warm;
    Warm.Emplace(Package, TEXT("/Script/Engine.MaterialInstanceConstant"));
    Warm.Emplace(Parent, TEXT("/Script/Engine.Material"));
    Warm.Emplace(Collection, TEXT("/Script/Engine.MaterialParameterCollection"));
    Warm.Emplace(TEXT("/Game/Carrot4/MasterMaterial/ParameterCollections/MPC_Env"),
        TEXT("/Script/Engine.MaterialParameterCollection"));
    Warm.Emplace(TEXT("/Game/Carrot4/MasterMaterial/Texture/T_Base_M"), TEXT("/Script/Engine.Texture2D"));
    Warm.Emplace(TEXT("/Game/Carrot4/MasterMaterial/Texture/T_Base_N"), TEXT("/Script/Engine.Texture2D"));
    Warm.Emplace(TEXT("/Game/Carrot4/Effects/Cmn/Materials/T_eft_Cmn_Awa00_M"), TEXT("/Script/Engine.Texture2D"));
    Warm.Emplace(TEXT("/Game/Carrot4/Effects/Cmn/Materials/T_eft_Cmn_Awa00_N"), TEXT("/Script/Engine.Texture2D"));
    Warm.Emplace(TEXT("/Game/Carrot4/Effects/Cmn/Materials/T_eft_Cmn_Indirect01_M"), TEXT("/Script/Engine.Texture2D"));
    TArray<TSharedPtr<FJsonValue>> WarmProof;
    for (const auto& Row : Warm)
    {
        UObject* Asset = LoadObject<UObject>(
            nullptr, *(Row.Key + TEXT(".") + FPackageName::GetShortName(Row.Key)), nullptr, LOAD_NoWarn | LOAD_Quiet);
        FP4OriginalRecord Record;
        FString ReadError;
        if (!TestTrue(TEXT("Required warm native dependency is loaded with its exact original identity: ") + Row.Key,
                Asset && P4ExactNativeString(Asset->GetClass()->GetPathName(), Row.Value) &&
                    !Asset->GetOutermost()->IsDirty() && !P4HasCreatorSource(Asset) && !P4HasCopiedGameData(Asset) &&
                    !P4HasIntrinsicGameProxy(Asset) && !P4IsVerifiedStandIn(Asset) &&
                    P4ReadOriginalRecord(Asset, Record, ReadError) && Record.Version == 2 &&
                    P4ExactNativeString(Record.Package, Row.Key) && P4ExactNativeString(Record.Class, Row.Value)))
            return false;
        WarmProof.Add(MakeShared<FJsonValueString>(Row.Key));
    }
    Proof->SetArrayField(TEXT("prevalidated_warm_native_packages"), WarmProof);
    const FString ObjectPath = Package + TEXT(".MI_eft_Cmn_Awa00_alcfr");
    auto* Original = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath);
    FP4OriginalRecord Before;
    FString Error;
    if (!TestTrue(TEXT("Exact unchanged old MIC baseline is required before the public route"),
            Original && P4ReadOriginalRecord(Original, Before, Error) && Before.Version == 2 && Before.ReadOnly &&
                Before.Kind == TEXT("object") && Before.ComparisonSchema == TEXT("object-lines-v1") &&
                Before.ComparisonDigest == TEXT("7110a081833b600ab18acee63fb926e5aba12dabd2dc020b847ba8a626739506") &&
                Before.SourceDigest == TEXT("df2e2d822afc58f105bf174978708a43eb378bf153c9132993fe0926c17387c6") &&
                Original->Parent && Original->Parent->GetOutermost()->GetName() == Parent &&
                !Original->GetOutermost()->IsDirty() && Original->ScalarParameterValues.Num() > 0))
        return false;
    const FString Marker = Original->GetOutermost()->GetMetaData()->GetValue(Original, TEXT("P4OriginalRecord"));
    const FString OriginalJson = P4MaterialInstanceJson(Original, Error);
    if (!TestTrue(TEXT("Original parameter values are readable"), !OriginalJson.IsEmpty() && Error.IsEmpty()))
        return false;
    TMap<FString, FString> FilesBefore;
    if (!TestTrue(TEXT("Original saved MIC bundle is hashed"), Bundle(FilesBefore)))
        return false;
    Proof->SetStringField(TEXT("original_record"), Marker);
    Proof->SetStringField(TEXT("original_parameters"), OriginalJson);
    Proof->SetObjectField(TEXT("original_files"), Json(FilesBefore));
    const bool AdmissionWitness = P4MICAdmissionWitness::Enabled(Package);
    Proof->SetBoolField(TEXT("admission_witness_enabled"), AdmissionWitness);
    if (AdmissionWitness &&
        !TestTrue(TEXT("Bounded native object lines before any public import are retained"),
            P4MICAdmissionWitness::Capture(Original, TEXT("before-first-run"), MakeShared<FJsonObject>())))
        return false;
    FCollectionCustody CollectionBefore;
    if (!CollectionControls(*this, Original, Out, Proof, CollectionBefore))
        return false;
    if (!MasterControls(*this, Original, Out, Proof, CollectionBefore))
        return false;
    int32 CollectionReloadEvents = 0, ParentReloadEvents = 0;
    const TWeakObjectPtr<UPackage> CollectionPackage(CollectionBefore.Object->GetOutermost());
    const TWeakObjectPtr<UPackage> ParentPackage(CollectionBefore.ParentObject->GetOutermost());
    const FDelegateHandle ReloadObserver = FCoreUObjectDelegates::OnPackageReloaded.AddLambda(
        [&](EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
        {
            // Stock PackageReload.h: PostPackageFixup runs after automatic
            // repointing. The old package may already be named DEADPACKAGE.
            if (Phase == EPackageReloadPhase::PostPackageFixup && Event &&
                ((Event->GetNewPackage() && P4ExactNativeString(Event->GetNewPackage()->GetName(), Collection)) ||
                    (CollectionPackage.IsValid() && Event->GetOldPackage() == CollectionPackage.Get())))
                ++CollectionReloadEvents;
            if (Phase == EPackageReloadPhase::PostPackageFixup && Event &&
                ((Event->GetNewPackage() && P4ExactNativeString(Event->GetNewPackage()->GetName(), Parent)) ||
                    (ParentPackage.IsValid() && Event->GetOldPackage() == ParentPackage.Get())))
                ++ParentReloadEvents;
        });
    ON_SCOPE_EXIT
    {
        FCoreUObjectDelegates::OnPackageReloaded.Remove(ReloadObserver);
        Proof->SetNumberField(TEXT("retained_collection_reload_events"), CollectionReloadEvents);
        Proof->SetNumberField(TEXT("retained_parent_reload_events"), ParentReloadEvents);
    };
    TMap<FMaterialParameterInfo, FGuid> ParentScalar, ParentVector, ParentTexture;
    const auto ParentFrame = [&](const TArray<FMaterialParameterInfo>& Infos, const TArray<FGuid>& Ids,
                                 TMap<FMaterialParameterInfo, FGuid>& Frame)
    {
        if (Infos.Num() != Ids.Num())
            return false;
        for (int32 I = 0; I < Infos.Num(); ++I)
        {
            if (Frame.Contains(Infos[I]) || !Ids[I].IsValid())
                return false;
            Frame.Add(Infos[I], Ids[I]);
        }
        return true;
    };
    TArray<FMaterialParameterInfo> BindingInfos;
    TArray<FGuid> BindingIds;
    CollectionBefore.ParentObject->GetAllScalarParameterInfo(BindingInfos, BindingIds);
    if (!TestTrue(TEXT("Known parent scalar binding frame is unique and valid"),
            ParentFrame(BindingInfos, BindingIds, ParentScalar)))
        return false;
    BindingInfos.Reset();
    BindingIds.Reset();
    CollectionBefore.ParentObject->GetAllVectorParameterInfo(BindingInfos, BindingIds);
    if (!TestTrue(TEXT("Known parent vector binding frame is unique and valid"),
            ParentFrame(BindingInfos, BindingIds, ParentVector)))
        return false;
    BindingInfos.Reset();
    BindingIds.Reset();
    CollectionBefore.ParentObject->GetAllTextureParameterInfo(BindingInfos, BindingIds);
    if (!TestTrue(TEXT("Known parent texture binding frame is unique and valid"),
            ParentFrame(BindingInfos, BindingIds, ParentTexture)))
        return false;
    bool BeforeCaptureObserved = false, BeforeCaptureBindingsExact = false;
    int32 BeforeCaptureKnown = 0, BeforeCaptureUnknown = 0;
    FString BeforeCaptureFrame, BeforeCaptureParameters;
    const FDelegateHandle FirstCookObserver = FCoreUObjectDelegates::OnObjectSaved.AddLambda(
        [&](UObject* Saved)
        {
            auto* Instance = Cast<UMaterialInstanceConstant>(Saved);
            if (BeforeCaptureObserved || !Instance ||
                Instance->GetClass() != UMaterialInstanceConstant::StaticClass() ||
                !P4ExactNativeString(Instance->GetOutermost()->GetName(), Package) ||
                Instance->Parent != CollectionBefore.ParentObject.Get())
                return;
            UMetaData* Meta = Instance->GetOutermost()->GetMetaData();
            if (Meta->HasValue(Instance, TEXT("P4OriginalRecord")) ||
                !Meta->HasValue(Instance, TEXT("P4MaterialInstanceNativeSourceV1")))
                return;
            // UObject::PreSave broadcasts this delegate in CaptureFresh's first
            // native cook, before its digest/baseline exists. Observe fields only.
            BeforeCaptureObserved = true;
            BeforeCaptureBindingsExact = true;
            const auto Check = [&](const auto& Rows, const TMap<FMaterialParameterInfo, FGuid>& Frame)
            {
                for (const auto& Row : Rows)
                    if (const FGuid* Id = Frame.Find(Row.ParameterInfo))
                    {
                        ++BeforeCaptureKnown;
                        BeforeCaptureBindingsExact &= Row.ExpressionGUID == *Id;
                    }
                    else
                    {
                        ++BeforeCaptureUnknown;
                        BeforeCaptureBindingsExact &= Row.ParameterInfo ==
                                FMaterialParameterInfo(TEXT("RefractionDepthBias")) &&
                            !Row.ExpressionGUID.IsValid();
                    }
            };
            Check(Instance->ScalarParameterValues, ParentScalar);
            Check(Instance->VectorParameterValues, ParentVector);
            Check(Instance->TextureParameterValues, ParentTexture);
            const auto* UnknownSwitch = Instance->GetStaticParameters().StaticSwitchParameters.FindByPredicate(
                [](const FStaticSwitchParameter& Row)
                { return Row.ParameterInfo == FMaterialParameterInfo(TEXT("ApplyNearFade")); });
            BeforeCaptureBindingsExact &= UnknownSwitch && UnknownSwitch->bOverride && UnknownSwitch->Value &&
                !UnknownSwitch->ExpressionGUID.IsValid();
            BeforeCaptureFrame = ParameterBindingFrame(Instance);
            FString ReadError;
            BeforeCaptureParameters = P4MaterialInstanceJson(Instance, ReadError);
            BeforeCaptureBindingsExact &= ReadError.IsEmpty() && !BeforeCaptureParameters.IsEmpty();
        });
    ON_SCOPE_EXIT
    {
        FCoreUObjectDelegates::OnObjectSaved.Remove(FirstCookObserver);
        Proof->SetBoolField(TEXT("first_native_cook_before_baseline_observed"), BeforeCaptureObserved);
        Proof->SetBoolField(TEXT("first_native_cook_bindings_exact"), BeforeCaptureBindingsExact);
        Proof->SetNumberField(TEXT("first_native_cook_known_bindings"), BeforeCaptureKnown);
        Proof->SetNumberField(TEXT("first_native_cook_unknown_zero_bindings"), BeforeCaptureUnknown);
        Proof->SetStringField(TEXT("first_native_cook_binding_frame"), BeforeCaptureFrame);
    };
    FP4KadanSourceCatalog Catalog;
    const FString Cache = FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals");
    if (ColdMode &&
        !TestFalse(TEXT("Cold condition begins without any converted-source cache"),
            IFileManager::Get().DirectoryExists(*Cache)))
        return false;
    const FString Paks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    const FString Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    const FString Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (!TestTrue(TEXT("Actual public Paks catalog reads"),
            !Paks.IsEmpty() && !Retoc.IsEmpty() && !Repak.IsEmpty() &&
                P4KadanReadSourceCatalog(Paks, Retoc, Catalog, Error)))
        return false;
    auto Settings = Catalog.ImportSettings;
    Settings.Repak = Repak;
    Settings.AllowGameBackedLevels = true;
    if (WarmMode)
    {
        FString CurrentCache;
        TArray<TSharedPtr<FP4KadanSourceAsset>> Cached;
        TArray<FString> Problems;
        if (!TestTrue(
                TEXT("Warm condition has a genuine current conversion receipt and exactly three raw prerequisites"),
                P4KadanRetainedConversionCacheCurrent(Settings, CurrentCache, Error) &&
                    P4KadanScanGame(CurrentCache, Cached, Problems) && Cached.Num() == 3 && Problems.Num() == 0))
            return false;
        TMap<FString, FString> Expected;
        Expected.Add(Parent, TEXT("605891bae709c9dc14b1b93b6e79f595e0dcbf698cdea74c495903223c257f1b"));
        Expected.Add(Collection, TEXT("9a7b73cffc976dece35d0d263311bfdd5c8ac6f0e7b097de8f83b45c95bbabd9"));
        Expected.Add(TEXT("/Game/Carrot4/MasterMaterial/ParameterCollections/MPC_Env"),
            TEXT("a4bcc90d13740c8bc429ec521267d802b60d2173103a01f84bb4ebaf082b4626"));
        FP4KadanImportSourceScope Sources(Cached);
        for (const auto& Source : Cached)
        {
            FP4KadanSourceBinding Binding;
            if (!TestTrue(TEXT("Warm raw prerequisite has the exact genuine source SHA: ") + Source->Package,
                    Expected.Contains(Source->Package) && P4KadanReadBoundSource(Source->Package, Binding, Error) &&
                        P4KadanSourceBindingCurrent(Binding, Error) &&
                        P4ExactNativeString(Binding.SourceDigest, Expected.FindRef(Source->Package))))
                return false;
        }
        Proof->SetBoolField(TEXT("warm_three_raw_sources_current"), true);
    }
    // No FP4MaterialRecovery/Recover call here. Ordinary family preparation must
    // establish the real parent recipe/read witness before original admission.
    const auto Run = [&](const TCHAR* Phase, FP4KadanImportResult& Result, TSharedPtr<FJsonObject>& Row) -> bool
    {
        const FString PriorPhase = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_ADMISSION_WITNESS_PHASE"));
        if (AdmissionWitness)
            FPlatformMisc::SetEnvironmentVar(TEXT("P4_NATIVE_MI_ADMISSION_WITNESS_PHASE"), Phase);
        ON_SCOPE_EXIT
        {
            if (AdmissionWitness)
                FPlatformMisc::SetEnvironmentVar(TEXT("P4_NATIVE_MI_ADMISSION_WITNESS_PHASE"), *PriorPhase);
        };
        if (!TestTrue(TEXT("Public catalog remains current"), P4KadanSourceCatalogCurrent(Catalog)))
            return false;
        const auto Plan = P4KadanPlanImport(Settings, {Package}, false);
        if (!TestEqual(FString(Phase) + TEXT(" plan has no problems"), Plan.Problems.Num(), 0))
            return false;
        if (!TestTrue(TEXT("Actual complete material execution route is enabled"),
                Plan.bMaterialExecutionDeferred && Plan.RequestedPackages.Contains(Package)))
            return false;
        TArray<TSharedPtr<FJsonValue>> Planned;
        TSet<FString> PlannedPackages;
        for (const auto& Asset : Plan.Assets)
        {
            Planned.Add(MakeShared<FJsonValueString>(Asset->Package));
            PlannedPackages.Add(Asset->Package);
        }
        Proof->SetArrayField(FString(Phase) + TEXT("_planned_native_packages"), Planned);
        if (ColdMode || WarmMode)
        {
            TSet<FString> ExpectedPlan{Package};
            if (WarmMode)
            {
                ExpectedPlan.Add(Parent);
                ExpectedPlan.Add(Collection);
                ExpectedPlan.Add(TEXT("/Game/Carrot4/MasterMaterial/ParameterCollections/MPC_Env"));
            }
            bool ExactPlan = PlannedPackages.Num() == ExpectedPlan.Num() && Plan.Assets.Num() == ExpectedPlan.Num();
            for (const auto& Expected : ExpectedPlan)
                ExactPlan &= PlannedPackages.Contains(Expected);
            if (!TestTrue(FString(Phase) +
                        TEXT(" actual plan exactly matches its separately qualified cold/warm source condition"),
                    ExactPlan))
                return false;
        }
        if (FString(Phase) == TEXT("edited-negative"))
        {
            auto* Edited = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath);
            if (!TestTrue(
                    TEXT(
                        "Edited-negative enters public RunImport with dirty=false, so refusal must see actual content"),
                    Edited && !Edited->GetOutermost()->IsDirty()))
                return false;
            Proof->SetBoolField(TEXT("negative_dirty_at_run_entry"), false);
        }
        Result = P4KadanRunImport(Settings, Plan, false);
        Row = Reply(Result);
        TestTrue(FString(Phase) + TEXT(" full public report retained"),
            FFileHelper::SaveStringToFile(Result.Report, *(Out / (FString(Phase) + TEXT(".import.json"))),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
        if (GShaderCompilingManager)
            GShaderCompilingManager->FinishAllCompilation();
        TSharedPtr<FJsonObject> FullReport;
        const TSharedPtr<FJsonObject>* AssetRows = nullptr;
        if (!TestTrue(TEXT("Public reply has the actual native asset ledger"),
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.Report), FullReport) &&
                    FullReport.IsValid() && FullReport->TryGetObjectField(TEXT("assets"), AssetRows)))
            return false;
        for (const auto& AssetRow : (*AssetRows)->Values)
            if (!P4ExactNativeString(AssetRow.Key, Package))
            {
                const TSharedPtr<FJsonObject>* RowObject = nullptr;
                if (!TestTrue(TEXT("Warm control creates/saves no native dependency: ") + AssetRow.Key,
                        AssetRow.Value->TryGetObject(RowObject) && !Flag(*RowObject, TEXT("created")) &&
                            !Flag(*RowObject, TEXT("saved"))))
                    return false;
            }

        const auto CollectionRow = Reply(Result, Collection);
        const auto ParentRow = Reply(Result, Parent);
        auto* Current = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath);
        auto* CurrentParent = Current ? Cast<UMaterial>(Current->Parent) : nullptr;
        FString ParentStateError;
        Proof->SetBoolField(FString(Phase) + TEXT("_parent_same_object"),
            CurrentParent && CurrentParent == CollectionBefore.ParentObject.Get());
        Proof->SetBoolField(FString(Phase) + TEXT("_camera_same_object"),
            LoadObject<UObject>(nullptr, *(Collection + TEXT(".MPC_Camera"))) == CollectionBefore.Object.Get());
        Proof->SetStringField(FString(Phase) + TEXT("_parent_native_state"),
            CurrentParent ? P4MaterialComparisonState(CurrentParent, ParentStateError) : FString());
        Proof->SetStringField(FString(Phase) + TEXT("_parent_state_error"), ParentStateError);
        const bool CollectionKept = ColdMode ? !CollectionRow.IsValid() && !Result.Imported.Contains(Collection)
                                             : Flag(CollectionRow, TEXT("valid")) &&
                !Flag(CollectionRow, TEXT("created")) && !Flag(CollectionRow, TEXT("saved")) &&
                Text(CollectionRow, TEXT("kind")) == TEXT("MaterialParameterCollection") &&
                Text(CollectionRow, TEXT("state")) == TEXT("existing unchanged source dependency retained") &&
                Result.Imported.FindRef(Collection) == TEXT("MaterialParameterCollection");
        const bool ParentKept = ColdMode
            ? !ParentRow.IsValid() && !Result.Imported.Contains(Parent) && !Result.ReferenceOnly.Contains(Parent)
            : Flag(ParentRow, TEXT("valid")) && !Flag(ParentRow, TEXT("created")) && !Flag(ParentRow, TEXT("saved")) &&
                Flag(ParentRow, TEXT("reference_only")) &&
                Text(ParentRow, TEXT("state")) == TEXT("existing unchanged source dependency retained") &&
                Result.ReferenceOnly.Contains(Parent) && Result.Imported.FindRef(Parent) == TEXT("Material");
        Proof->SetBoolField(FString(Phase) + TEXT("_master_retention_branch_observed"), !ColdMode && ParentKept);
        Proof->SetBoolField(FString(Phase) + TEXT("_camera_retention_branch_observed"), !ColdMode && CollectionKept);
        if (!TestTrue(FString(Phase) +
                    TEXT(
                        " retains the exact unchanged source Camera without any reload, preserving full original parent state"),
                CollectionKept && ParentKept && CollectionReloadEvents == 0 && ParentReloadEvents == 0 &&
                    RetainedCustody(Current, CollectionBefore)))
            return false;
        Proof->SetBoolField(FString(Phase) + TEXT("_collection_and_parent_unchanged"), true);
        if (ColdMode || WarmMode)
        {
            const TSharedPtr<FJsonObject>* Family = nullptr;
            const TArray<TSharedPtr<FJsonValue>>* Used = nullptr;
            bool EnvUsed = false, OnlyEnv = true;
            bool HasUsed = Row.IsValid() && Row->TryGetObjectField(TEXT("material_family_sources"), Family) &&
                (*Family)->TryGetArrayField(TEXT("typed_used_resources"), Used);
            if (HasUsed)
                for (const auto& Resource : *Used)
                {
                    const TSharedPtr<FJsonObject>* UsedRow = nullptr;
                    if (!Resource->TryGetObject(UsedRow))
                    {
                        OnlyEnv = false;
                        break;
                    }
                    if (Text(*UsedRow, TEXT("native_class")) != TEXT("/Script/Engine.MaterialParameterCollection"))
                        continue;
                    const bool ExactEnv = Text(*UsedRow, TEXT("package")) ==
                            TEXT("/Game/Carrot4/MasterMaterial/ParameterCollections/MPC_Env") &&
                        Text(*UsedRow, TEXT("source_sha256")) ==
                            TEXT("a4bcc90d13740c8bc429ec521267d802b60d2173103a01f84bb4ebaf082b4626");
                    EnvUsed |= ExactEnv;
                    OnlyEnv &= ExactEnv;
                }
            if (!TestTrue(FString(Phase) +
                        TEXT(
                            " actual shader-used collection ledger includes exact Env and excludes metadata-only Camera"),
                    HasUsed && EnvUsed && OnlyEnv))
                return false;
            Proof->SetBoolField(FString(Phase) + TEXT("_actual_used_env_only"), true);
        }

        return TestTrue(
            FString(Phase) + TEXT(" public import completed and target has actual read-only parent recipe witness"),
            Result.Ok && !Result.Cancelled && !Result.NotImported.Contains(Package) && Flag(Row, TEXT("valid")) &&
                Witness(Row));
    };
    // Dirty=false deliberately prevents the package-dirty guard from masking a
    // semantic comparison failure. Only this real value edit causes refusal.
    const auto Info = Original->ScalarParameterValues[0].ParameterInfo;
    const float Value = Original->ScalarParameterValues[0].ParameterValue;
    const float Edited = Value + 0.125f;
    Original->ScalarParameterValues[0].ParameterValue = Edited;
    Original->GetOutermost()->SetDirtyFlag(false);
    TWeakObjectPtr<UMaterialInstanceConstant> EditedObject(Original);
    bool RestoreEdit = true;
    ON_SCOPE_EXIT
    {
        if (RestoreEdit && EditedObject.IsValid() && EditedObject->GetPathName() == ObjectPath &&
            EditedObject->ScalarParameterValues.Num() > 0 &&
            EditedObject->ScalarParameterValues[0].ParameterInfo == Info)
        {
            EditedObject->ScalarParameterValues[0].ParameterValue = Value;
            EditedObject->GetOutermost()->SetDirtyFlag(false);
        }
    };
    FP4KadanImportResult Negative;
    TSharedPtr<FJsonObject> NegativeRow;
    if (!Run(TEXT("edited-negative"), Negative, NegativeRow))
        return false;
    Original = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath);
    TMap<FString, FString> NegativeFiles;
    if (!TestTrue(TEXT("Edited MIC stays the same original, with its actual edit, custody and saved bytes retained"),
            Text(NegativeRow, TEXT("state")) == TEXT("existing creator content preserved") &&
                !Flag(NegativeRow, TEXT("saved")) && Original && Original == EditedObject.Get() &&
                Original->ScalarParameterValues.Num() > 0 && Original->ScalarParameterValues[0].ParameterInfo == Info &&
                Original->ScalarParameterValues[0].ParameterValue == Edited &&
                P4ExactNativeString(
                    Marker, Original->GetOutermost()->GetMetaData()->GetValue(Original, TEXT("P4OriginalRecord"))) &&
                Bundle(NegativeFiles) && Same(FilesBefore, NegativeFiles)))
        return false;
    Proof->SetObjectField(TEXT("negative_files"), Json(NegativeFiles));
    Proof->SetStringField(TEXT("negative_import_state"), Text(NegativeRow, TEXT("state")));
    // A refreshed dependency may legitimately dirty its referencers. Entry was
    // proved clean above; disk/custody/value preservation are the negative gate.
    Proof->SetBoolField(TEXT("negative_dirty_after_dependency_reimport"), Original->GetOutermost()->IsDirty());
    Proof->SetBoolField(TEXT("negative_preserved_existing"), true);
    Proof->SetNumberField(TEXT("edited_scalar_value"), Edited);
    Original->ScalarParameterValues[0].ParameterValue = Value;
    Original->GetOutermost()->SetDirtyFlag(false);
    RestoreEdit = false;
    if (!TestTrue(TEXT("Restoring only the test value restores exact original parameter content"),
            P4ExactNativeString(P4MaterialInstanceJson(Original, Error), OriginalJson) && Error.IsEmpty()))
        return false;
    if (AdmissionWitness &&
        !TestTrue(TEXT("Bounded native object lines after negative scalar restoration are retained"),
            P4MICAdmissionWitness::Capture(Original, TEXT("after-negative-restore"), MakeShared<FJsonObject>())))
        return false;
    FP4KadanImportResult Positive;
    TSharedPtr<FJsonObject> PositiveRow;
    if (!Run(TEXT("unchanged-positive"), Positive, PositiveRow))
        return false;
    auto* Fresh = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath);
    FP4OriginalRecord After;
    TMap<FString, FString> FilesAfter;
    FString FreshReadError;
    const bool FreshRecord = Fresh && P4ReadOriginalRecord(Fresh, After, FreshReadError);
    const auto FreshDiagnostic = Fresh ? P4DiagnoseOriginalAsset(Fresh) : FP4OriginalDiagnostic();
    FP4MaterialInstanceNativeEvidence FreshEvidence;
    FString FreshStateError;
    const FString FreshState = Fresh ? P4MaterialInstanceComparisonState(Fresh, FreshStateError, &FreshEvidence)
                                     : FString();
    Proof->SetBoolField(TEXT("positive_same_old_object"), Fresh && Fresh == EditedObject.Get());
    Proof->SetBoolField(TEXT("positive_current_dirty"), Fresh && Fresh->GetOutermost()->IsDirty());
    Proof->SetBoolField(TEXT("positive_record_read"), FreshRecord);
    Proof->SetStringField(TEXT("positive_record_error"), FreshReadError);
    Proof->SetBoolField(TEXT("positive_reply_saved"), Flag(PositiveRow, TEXT("saved")));
    Proof->SetStringField(TEXT("positive_reply_state"), Text(PositiveRow, TEXT("state")));
    Proof->SetBoolField(TEXT("positive_imported_ledger"), Positive.Imported.Contains(Package));
    Proof->SetBoolField(TEXT("positive_reference_only_ledger"), Positive.ReferenceOnly.Contains(Package));
    Proof->SetStringField(TEXT("positive_record_kind"), After.Kind);
    Proof->SetStringField(TEXT("positive_record_source_kind"), After.SourceDigestKind);
    Proof->SetStringField(TEXT("positive_record_schema"), After.ComparisonSchema);
    Proof->SetStringField(TEXT("positive_record_source_md5"), After.SourceDigest);
    Proof->SetStringField(TEXT("positive_record_native_baseline"), After.ComparisonDigest);
    Proof->SetStringField(TEXT("positive_diagnostic_error"), FreshDiagnostic.Error);
    Proof->SetStringField(TEXT("positive_diagnostic_current"), FreshDiagnostic.CurrentDigest);
    Proof->SetStringField(TEXT("positive_native_state"), FreshState);
    Proof->SetStringField(TEXT("positive_native_state_error"), FreshStateError);
    Proof->SetStringField(TEXT("positive_cooked_object_state"), FreshEvidence.NativeDigest);
    Proof->SetStringField(TEXT("positive_transport_sha256"), FreshEvidence.TransportDigest);
    Proof->SetBoolField(TEXT("positive_transport_present"), FreshEvidence.HasTransport);
    Proof->SetStringField(TEXT("positive_raw_source_sha256"), FreshEvidence.SourceSha256);
    if (!TestTrue(TEXT("Unchanged old MIC is newly source-built, saved and reloaded rather than preserved"),
            Fresh && Fresh != EditedObject.Get() &&
                Text(PositiveRow, TEXT("state")) != TEXT("existing creator content preserved") &&
                Flag(PositiveRow, TEXT("saved")) && !Flag(PositiveRow, TEXT("reference_only")) &&
                !Positive.ReferenceOnly.Contains(Package) && Positive.Imported.Contains(Package) &&
                P4ReadOriginalRecord(Fresh, After, Error) && After.Kind == TEXT("material-instance") &&
                After.ComparisonSchema == TEXT("material-instance-native-state-v1") &&
                After.SourceDigestKind == TEXT("material-instance-v2-md5") && !Fresh->GetOutermost()->IsDirty() &&
                Bundle(FilesAfter) && !Same(FilesBefore, FilesAfter) &&
                FreshDiagnostic.Comparison == EP4OriginalComparison::Equal && FreshDiagnostic.Error.IsEmpty() &&
                FreshStateError.IsEmpty() && P4ExactNativeString(FreshState, FreshDiagnostic.CurrentDigest) &&
                P4ExactNativeString(FreshState, After.ComparisonDigest) &&
                P4ExactNativeString(FreshEvidence.SourceSha256,
                    TEXT("df2e2d822afc58f105bf174978708a43eb378bf153c9132993fe0926c17387c6"))))
        return false;
    FString ColdParametersError;
    const FString ColdParameters = P4MaterialInstanceJson(Fresh, ColdParametersError);
    if (!TestTrue(
            TEXT("First fresh baseline already contains all 12 real parent bindings and only the unknown zero binding"),
            BeforeCaptureObserved && BeforeCaptureBindingsExact && BeforeCaptureKnown == 12 &&
                BeforeCaptureUnknown == 1))
        return false;
    if (!TestTrue(TEXT("Cold reload preserves the exact pre-baseline native binding frame and all parameter values"),
            P4ExactNativeString(ParameterBindingFrame(Fresh), BeforeCaptureFrame) && ColdParametersError.IsEmpty() &&
                P4ExactNativeString(ColdParameters, BeforeCaptureParameters)))
        return false;
    Proof->SetBoolField(TEXT("first_native_cook_bindings_survive_cold_reload"), true);
    auto* Resource = Fresh->GetMaterialResource(GMaxRHIFeatureLevel);
    TestTrue(TEXT("Actual reloaded MIC has a valid shader map with no compile errors"),
        Resource && Resource->HasValidGameThreadShaderMap() && Resource->GetCompileErrors().Num() == 0);
    if (!FreshNativeControls(*this, Fresh, Proof) ||
        !TestTrue(TEXT("Fresh comparison controls leave full parent and collection custody unchanged"),
            RetainedCustody(Fresh, CollectionBefore)))
        return false;
    Proof->SetObjectField(TEXT("positive_files"), Json(FilesAfter));
    Proof->SetBoolField(TEXT("positive_fresh_source_built"), true);
    Proof->SetBoolField(TEXT("positive_saved"), Flag(PositiveRow, TEXT("saved")));
    Proof->SetStringField(TEXT("positive_import_state"), Text(PositiveRow, TEXT("state")));
    Proof->SetStringField(TEXT("positive_schema"), After.ComparisonSchema);
    Proof->SetStringField(TEXT("positive_source_recipe_md5"), After.SourceDigest);
    Proof->SetStringField(TEXT("positive_parameters"), P4MaterialInstanceJson(Fresh, Error));
    return !HasAnyErrors();
}
#endif

#include "P4LegacyParticleReference.h"
#include "P4ParticleReference.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "P4KadanReadOnly.h"
#include "P4OriginalImportSave.h"
#include "AssetRegistryModule.h"
#include "Curves/CurveFloat.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Particles/ParticleSystem.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CanonicalLegacyParticleReferenceTest, "P4MapKit.Particles.CanonicalLegacyReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CanonicalLegacyParticleReferenceTest::RunTest(const FString&)
{
    P4ImportPublication::FImportScope Import;
    const FString Name = TEXT("PS_LegacyProof_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FP4OriginalAssetImportResult Missing;
    Missing.Package = TEXT("/Game/Mods/LegacyParticleProof/") + Name;
    Missing.AssetPath = Missing.Package + TEXT(".") + Name;
    Missing.Class = TEXT("/Script/Engine.ParticleSystem");
    Missing.AssetIdentityKnown = true;
    Missing.ObjectsKnown = true;
    Missing.Error = EP4OriginalAssetImportError::ClassUnavailable;
    Missing.ErrorDetail = TEXT(
        "asset-import-class-not-in-the-project: /Script/Engine.ParticleModuleLocationPrimitiveC4Sphere");
    const auto Made = P4KeepUnsupportedParticleReference(Missing, false);
    TStrongObjectPtr<UParticleSystem> Asset(Cast<UParticleSystem>(Made.Asset));
    if (!TestTrue(TEXT("The exact historical producer creates this isolated identity-only fixture"),
            Made.IsValid() && Asset.IsValid()))
        return false;
    UPackage* Package = Asset->GetOutermost();
    UMetaData* Meta = Package->GetMetaData();
    const FString Marker = Meta->GetValue(Asset.Get(), TEXT("P4OriginalRecord"));
    ON_SCOPE_EXIT
    {
        Meta->RemoveValue(Asset.Get(), TEXT("P4OriginalRecord"));
        P4Kadan::UpdateReadOnlyAsset(Asset.Get());
        FAssetRegistryModule::AssetDeleted(Asset.Get());
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->MarkPendingKill();
        Package->SetDirtyFlag(false);
    };
    Package->SetDirtyFlag(false);
    FString Error;
    if (!TestTrue(
            TEXT("The actual native cook plus all inherited/editor persistent fields prove only canonical empty state"),
            P4ProveCanonicalLegacyParticleReference(Asset.Get(), Error)))
    {
        AddError(Error);
        return false;
    }
    const auto Refuses = [&]()
    {
        Error.Reset();
        return !P4ProveCanonicalLegacyParticleReference(Asset.Get(), Error) && !Error.IsEmpty();
    };
    const float Warmup = Asset->WarmupTime;
    Asset->WarmupTime = 1.f;
    TestTrue(TEXT("A current authored warmup edit is never discarded"), Refuses());
    TestEqual(TEXT("The refused edit remains in the object"), Asset->WarmupTime, 1.f);
    Asset->WarmupTime = -0.f;
    TestTrue(
        TEXT("A floating-point word edit remains strict even when native numeric equality treats signed zero alike"),
        Refuses());
    Asset->WarmupTime = Warmup;
    const FBox Bounds = Asset->FixedRelativeBoundingBox;
    Asset->FixedRelativeBoundingBox.Min.X += 1.f;
    TestTrue(TEXT("A native reflected struct member edit refuses"), Refuses());
    Asset->FixedRelativeBoundingBox = Bounds;
#if WITH_EDITORONLY_DATA
    const float Thumbnail = Asset->ThumbnailWarmup;
    Asset->ThumbnailWarmup += 1.f;
    TestTrue(TEXT("An editor-only persistent thumbnail edit refuses even though cooking omits it"), Refuses());
    Asset->ThumbnailWarmup = Thumbnail;
#endif
    Asset->Emitters.Add(nullptr);
    TestTrue(TEXT("Even an empty emitter slot is authored noncanonical array content"), Refuses());
    Asset->Emitters.Empty();
    if (!TestFalse(TEXT("The extra-object controls use a concrete native class"),
            UCurveFloat::StaticClass()->HasAnyClassFlags(CLASS_Abstract)))
        return false;
    UObject* Child = NewObject<UCurveFloat>(Asset.Get(), TEXT("CreatorChild"), RF_Transactional);
    if (!TestNotNull(TEXT("The actual persistent child control was constructed"), Child))
        return false;
    TestTrue(TEXT("An unreflected persistent child refuses"), Refuses());
    Child->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
    Child->MarkPendingKill();
    UObject* Extra = NewObject<UCurveFloat>(Package, TEXT("CreatorAsset"), RF_Public | RF_Standalone);
    if (!TestNotNull(TEXT("The actual second package-asset control was constructed"), Extra))
        return false;
    TestTrue(TEXT("A second package asset refuses"), Refuses());
    Extra->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
    Extra->MarkPendingKill();
    Package->SetDirtyFlag(true);
    TestTrue(TEXT("Dirty content refuses"), Refuses());
    Package->SetDirtyFlag(false);
    for (const TCHAR* Key : {TEXT("P4KadanCreatorSource"), TEXT("P4KadanCopiedGameAsset"), TEXT("P4BlueprintStandIn"),
             TEXT("P4NativeObjectBaseline"), TEXT("P4NativeObjectValuesNotHeld"), TEXT("P4GameStockTags")})
    {
        Meta->SetValue(Asset.Get(), Key, TEXT(""));
        TestTrue(
            TEXT(
                "Even an empty extra creator/copy/custody/unsupported marker is not the authentic historical identity state"),
            Refuses());
        Meta->RemoveValue(Asset.Get(), Key);
    }
    const auto RecordEdit = [&](const TCHAR* Key, const FString& Value)
    {
        TSharedPtr<FJsonObject> Json;
        FString Edited;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Marker), Json) || !Json.IsValid())
            return false;
        Json->SetStringField(Key, Value);
        FJsonSerializer::Serialize(
            Json.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Edited));
        Meta->SetValue(Asset.Get(), TEXT("P4OriginalRecord"), *Edited);
        const bool Refused = Refuses();
        Meta->SetValue(Asset.Get(), TEXT("P4OriginalRecord"), *Marker);
        return Refused;
    };
    TestTrue(TEXT("Changed record package refuses"), RecordEdit(TEXT("package"), Missing.Package + TEXT("_Copy")));
    TestTrue(TEXT("Changed record class refuses"),
        RecordEdit(TEXT("class"), TEXT("/Script/Engine.P4RecoveredParticleSystem")));
    TestTrue(TEXT("Case-varied schema is not this exact old producer"),
        RecordEdit(TEXT("comparison_schema"), TEXT("Particle-identity-reference-v1")));
    TestTrue(TEXT("An invented prior source digest refuses"),
        RecordEdit(TEXT("source_digest"), FString::ChrN(64, TEXT('a'))));
    TestTrue(TEXT("An invented comparison baseline refuses"),
        RecordEdit(TEXT("comparison_digest"), FString::ChrN(64, TEXT('a'))));
    {
        TSharedPtr<FJsonObject> Json;
        FString Edited;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Marker), Json))
            return false;
        Json->RemoveField(TEXT("values"));
        FJsonSerializer::Serialize(
            Json.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Edited));
        Meta->SetValue(Asset.Get(), TEXT("P4OriginalRecord"), *Edited);
        TestTrue(TEXT("A missing explicit unsupported-note ledger refuses"), Refuses());
        Meta->SetValue(Asset.Get(), TEXT("P4OriginalRecord"), *Marker);
    }
    {
        TSharedPtr<FJsonObject> Json;
        FString Edited;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Marker), Json))
            return false;
        Json->SetArrayField(TEXT("values"), {});
        FJsonSerializer::Serialize(
            Json.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Edited));
        Meta->SetValue(Asset.Get(), TEXT("P4OriginalRecord"), *Edited);
        TestTrue(TEXT("Removing the historical missing-module note does not create admission"), Refuses());
        Meta->SetValue(Asset.Get(), TEXT("P4OriginalRecord"), *Marker);
    }
    TArray<FString> Canonical = {Name + TEXT(" | class /Script/Engine.ParticleSystem, kind 00000003"),
        Name + TEXT(" | made from /Script/Engine.Default__ParticleSystem")};
    TestTrue(TEXT("Exactly the canonical native line pair is accepted"),
        P4LegacyParticleReference::CanonicalCookLines(Name, Canonical));
    for (const TCHAR* Added :
        {TEXT("id for lazy references 1234"), TEXT("native data of 8 bytes"), TEXT("warmuptime = 1")})
    {
        auto Changed = Canonical;
        Changed.Add(Name + TEXT(" | ") + Added);
        TestFalse(TEXT("No GUID, native payload or property line can be hidden by a prefix match"),
            P4LegacyParticleReference::CanonicalCookLines(Name, Changed));
    }
    TestTrue(TEXT("All control edits are restored only by this disposable test, never by the proof"),
        P4ProveCanonicalLegacyParticleReference(Asset.Get(), Error));
    const FP4OriginalStatus Status = P4QueryOriginalAsset(Asset.Get());
    TestTrue(
        TEXT("The proof invents no source digest or comparison baseline and leaves the original record byte-exact"),
        Status.Record.SourceDigest.IsEmpty() && Status.Record.ComparisonDigest.IsEmpty() &&
            P4ExactNativeString(Meta->GetValue(Asset.Get(), TEXT("P4OriginalRecord")), Marker) && !Package->IsDirty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RetainedLegacyParticleReferenceProof,
    "P4MapKit.Particles.RetainedLegacyReferenceProof",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RetainedLegacyParticleReferenceProof::RunTest(const FString&)
{
    const FString Target = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_LEGACY_PARTICLE_PROOF_PROJECT"));
    const FString SavedRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_LEGACY_PARTICLE_PROOF_SAVED_ROOT"));
    const FString SourceRoot = FPlatformMisc::GetEnvironmentVariable(
        TEXT("P4_NATIVE_LEGACY_PARTICLE_PROOF_SOURCE_ROOT"));
    if (Target.IsEmpty() || SavedRoot.IsEmpty() || SourceRoot.IsEmpty())
    {
        AddWarning(TEXT(
            "P4MapKit test not run. Five retained identity-stub copies in a separately owned fixture are required."));
        return true;
    }
    const auto Directory = [](FString Path)
    {
        Path = FPaths::ConvertRelativePathToFull(Path);
        FPaths::RemoveDuplicateSlashes(Path);
        FPaths::NormalizeDirectoryName(Path);
        return Path;
    };
    const auto SameDirectory = [&](const FString& A, const FString& B)
    { return FPaths::IsSamePath(Directory(A), Directory(B)); };
    const auto IndependentInput = [&](const FString& Input)
    {
        const FString Path = Directory(Input), Content = Directory(FPaths::ProjectContentDir());
        return !SameDirectory(Path, Content) && !FPaths::IsUnderDirectory(Path, Content);
    };
    if (!TestTrue(TEXT("The explicit owned target is separate from the immutable fixture and source roots"),
            SameDirectory(Target, FPaths::ProjectDir()) && IndependentInput(SavedRoot) && IndependentInput(SourceRoot)))
        return false;
    TestTrue(TEXT("Duplicate target separators still name the same owned directory"),
        SameDirectory(Target + TEXT("//"), FPaths::ProjectDir()));
    TestFalse(TEXT("A source or baseline root cannot be the owned Content with extra separators"),
        IndependentInput(FPaths::ProjectContentDir() + TEXT("//")));
    TestFalse(TEXT("A source or baseline root cannot be inside the owned Content"),
        IndependentInput(FPaths::ProjectContentDir() / TEXT("OldFiles")));
    TestTrue(TEXT("A genuine Content sibling remains independent"),
        IndependentInput(Directory(FPaths::ProjectContentDir()) + TEXT("_Sibling")));
    const FString ResultFile = FPaths::ProjectSavedDir() / TEXT("P4MapKit/LegacyParticleProof/result.json");
    if (!TestTrue(TEXT("A prior or uncertain fixed proof receipt must be preserved before this selector can run"),
            !IFileManager::Get().FileExists(*ResultFile) && !IFileManager::Get().DirectoryExists(*ResultFile)))
        return false;
    const TArray<FString> Names = {TEXT("/Game/Carrot4/Effects/Player/Pikmin/Demo/PS_eft_DemoPikmin_PhotonBirth00"),
        TEXT("/Game/Carrot4/Effects/Player/Pikmin/Demo/PS_eft_DemoPikmin_PhotonBirth01"),
        TEXT("/Game/Carrot4/Effects/Player/Pikmin/Demo/PS_eft_DemoPikmin_PhotonBirth02"),
        TEXT("/Game/Carrot4/Effects/Player/Pikmin/PS_eft_Pikmin_AtkShotPhotonCharge00"),
        TEXT("/Game/Carrot4/Effects/Player/Pikmin/PS_eft_Pikmin_AtkShotPhotonCharge01")};
    TArray<FString> Inputs;
    const auto Fingerprint = [](const FString& File)
    {
        if (!IFileManager::Get().FileExists(*File))
            return FString(TEXT("absent"));
        int64 Bytes = 0;
        FString Digest;
        if (!P4HashFile(File, 256 * 1024 * 1024, Bytes, Digest))
            return FString(TEXT("unreadable"));
        return Digest +
            FString::Printf(TEXT("/%lld/%lld/%d"), Bytes, IFileManager::Get().GetTimeStamp(*File).GetTicks(),
                IFileManager::Get().IsReadOnly(*File) ? 1 : 0);
    };
    for (const FString& Package : Names)
    {
        const FString Relative = Package.Mid(6);
        const FString Saved = SavedRoot / (Relative + TEXT(".uasset"));
        const FString Local = FPaths::ProjectContentDir() / (Relative + TEXT(".uasset"));
        int64 Bytes = 0;
        FString OriginalHash, LocalHash;
        if (!TestTrue(TEXT("Each proof asset begins as an exact actual legacy-stub copy: ") + Package,
                P4HashFile(Saved, 4 * 1024 * 1024, Bytes, OriginalHash) &&
                    P4HashFile(Local, 4 * 1024 * 1024, Bytes, LocalHash) &&
                    P4ExactNativeString(OriginalHash, LocalHash)))
            return false;
        if (!TestTrue(TEXT("The actual complete source pair is present for unchanged-input custody only"),
                IFileManager::Get().FileExists(*(SourceRoot / (Relative + TEXT(".uasset")))) &&
                    IFileManager::Get().FileExists(*(SourceRoot / (Relative + TEXT(".uexp"))))))
            return false;
        for (const TCHAR* Extension : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")})
            for (const FString& Root : {SavedRoot, SourceRoot, FPaths::ProjectContentDir()})
                Inputs.Add(Root / (Relative + Extension));
    }
    const int32 RuntimeBegin = Inputs.Num();
    for (const TCHAR* File : {TEXT("Binaries/Win64/UE4Editor-Kadan.dll"),
             TEXT("Binaries/Win64/UE4Editor-P4BlueprintImport.dll"), TEXT("Binaries/Win64/UE4Editor-P4MapKitTests.dll"),
             TEXT("Binaries/Win64/UE4Editor.modules"), TEXT("Kadan.uplugin"), TEXT("Config/DefaultKadan.ini")})
        Inputs.Add(FPaths::ProjectPluginsDir() / TEXT("Kadan") / File);
    for (const TCHAR* File :
        {TEXT("Binaries/Win64/UE4Editor-Carrot4.dll"), TEXT("Binaries/Win64/UE4Editor-AkAudio.dll"),
            TEXT("Binaries/Win64/UE4Editor-EasyParticle.dll"), TEXT("Binaries/Win64/UE4Editor-UIParticle.dll"),
            TEXT("Binaries/Win64/UE4Editor-SwitchRuntimeSettings.dll"),
            TEXT("Binaries/Win64/UE4Editor-P4StubsCore.dll"), TEXT("Binaries/Win64/UE4Editor.modules"),
            TEXT("P4Stubs.uplugin")})
        Inputs.Add(FPaths::ProjectPluginsDir() / TEXT("P4Stubs") / File);
    TMap<FString, FString> Before;
    for (int32 Index = 0; Index < Inputs.Num(); ++Index)
    {
        const FString& File = Inputs[Index];
        const FString State = Fingerprint(File);
        if (!TestTrue(TEXT("Every configured input is readable, or an explicitly absent optional sidecar"),
                State != TEXT("unreadable")))
            return false;
        if (Index >= RuntimeBegin &&
            !TestTrue(TEXT("All runtime6 and SDK8 files are present"), State != TEXT("absent")))
            return false;
        Before.Add(File, State);
    }
    auto Receipt = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Cases;
    ON_SCOPE_EXIT
    {
        bool Unchanged = true;
        for (const auto& Pair : Before)
            Unchanged &= P4ExactNativeString(Pair.Value, Fingerprint(Pair.Key));
        TestTrue(
            TEXT(
                "All configured saved/source/target bundles, runtime6/SDK8 hashes, size, mtime and read-only attributes are unchanged"),
            Unchanged);
        Receipt->SetBoolField(TEXT("input_custody_unchanged"), Unchanged);
        Receipt->SetBoolField(TEXT("replacement_admitted"), false);
        Receipt->SetBoolField(TEXT("source_payload_imported"), false);
        Receipt->SetBoolField(TEXT("passed"), !HasAnyErrors());
        Receipt->SetArrayField(TEXT("cases"), Cases);
        auto Custody = MakeShared<FJsonObject>();
        for (const auto& Pair : Before)
            Custody->SetStringField(Pair.Key, Pair.Value);
        Receipt->SetObjectField(TEXT("inputs_before"), Custody);
        FString Text;
        FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<TCHAR>::Create(&Text));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ResultFile), true);
        if (!FFileHelper::SaveStringToFile(Text, *ResultFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
                &IFileManager::Get(), FILEWRITE_NoReplaceExisting))
            AddError(TEXT("The current-state-only proof receipt could not be saved without replacing prior evidence."));
    };
    P4ImportPublication::FImportScope Import;
    for (const FString& Package : Names)
    {
        auto Case = MakeShared<FJsonObject>();
        Case->SetStringField(TEXT("package"), Package);
        TStrongObjectPtr<UObject> Asset(
            LoadObject<UObject>(nullptr, *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package))));
        FString Error;
        const bool Proven = Asset.IsValid() && P4ProveCanonicalLegacyParticleReference(Asset.Get(), Error);
        Case->SetBoolField(TEXT("canonical_current_state_proven"), Proven);
        Case->SetStringField(TEXT("error"), Error);
        Cases.Add(MakeShared<FJsonValueObject>(Case));
        TestTrue(TEXT("Actual retained current-state-only proof: ") + Package + TEXT(" ") + Error, Proven);
    }
    return !HasAnyErrors();
}
#endif

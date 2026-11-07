#include "P4KadanImport.h"
#include "P4KadanImportSources.h"
#include "P4LegacyVectorFieldCompatibility.h"
#include "P4LevelContent.h"
#include "P4NativeHash.h"
#include "P4OriginalImportSave.h"
#include "P4OriginalRecord.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "VectorField/VectorFieldStatic.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RetainedVectorFieldLegacy, "P4MapKit.Kadan.RetainedVectorFieldLegacy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RetainedVectorFieldLegacy::RunTest(const FString&)
{
    const FString Target = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_LEGACY_VECTOR_PROJECT"));
    const FString SourceRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_LEGACY_VECTOR_SOURCE_ROOT"));
    const FString SavedRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_LEGACY_VECTOR_SAVED_ROOT"));
    if (Target.IsEmpty() || SourceRoot.IsEmpty() || SavedRoot.IsEmpty())
    {
        AddWarning(
            TEXT("P4MapKit test not run. Two retained vector fixtures in a separately owned project are required."));
        return true;
    }
    const auto Directory = [](FString Path)
    {
        Path = FPaths::ConvertRelativePathToFull(Path);
        // NormalizeDirectoryName deliberately preserves a trailing double slash.
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
    const auto OwnsFixture = [&](const FString& Configured, const FString& Baseline)
    { return SameDirectory(Configured, FPaths::ProjectDir()) && IndependentInput(Baseline); };
    AddInfo(TEXT("Legacy vector owned-project guard rawtarget=[") + Target + TEXT("] rawproject=[") +
        FPaths::ProjectDir() + TEXT("] target=") + Directory(Target) + TEXT(" project=") +
        Directory(FPaths::ProjectDir()) + TEXT(" saved=") + Directory(SavedRoot) + TEXT(" content=") +
        Directory(FPaths::ProjectContentDir()));
    if (!TestTrue(TEXT("Only the explicitly owned disposable project can publish these two controls"),
            OwnsFixture(Target, SavedRoot)))
        return false;
    TestTrue(TEXT("The exact owned directory is unchanged by a trailing separator"),
        OwnsFixture(Target + TEXT("/"), SavedRoot));
    TestTrue(TEXT("Duplicate target separators still name the same owned directory"),
        OwnsFixture(Target + TEXT("//"), SavedRoot));
    TestFalse(TEXT("A baseline with the owned Content path cannot pass by changing its trailing separator"),
        OwnsFixture(Target, FPaths::ProjectContentDir() + TEXT("/")));
    TestFalse(TEXT("Multiple trailing separators cannot disguise the owned Content baseline"),
        OwnsFixture(Target, FPaths::ProjectContentDir() + TEXT("//")));
    TestFalse(TEXT("A descendant of owned Content is not an independent baseline"),
        OwnsFixture(Target, FPaths::ProjectContentDir() / TEXT("OldFiles")));
    TestTrue(TEXT("A genuine Content sibling is not rejected by a textual prefix match"),
        OwnsFixture(Target, Directory(FPaths::ProjectContentDir()) + TEXT("_Sibling")));
    TestFalse(TEXT("A genuinely different target directory cannot pass normalization"),
        OwnsFixture(Target / TEXT("OtherProject"), SavedRoot));
    P4ImportPublication::FImportScope Import;
    const TArray<FString> Names = {TEXT("/Game/Carrot4/Effects/Teki/VF_eft_Teki_Particle00"),
        TEXT("/Game/Carrot4/Effects/Objects/Onyon/VF_eft_Onyon_Particle02")};
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TMap<FString, FString> OldDigests, FixtureDigests;
    auto Receipt = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Cases;
    const auto ObjectPath = [](const FString& Package)
    { return Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package); };
    const auto FileDigest = [](const FString& File)
    {
        TArray<uint8> Bytes;
        FString Digest;
        return FFileHelper::LoadFileToArray(Bytes, *File) &&
                P4HashSourceBuffers(Bytes.GetData(), Bytes.Num(), nullptr, 0, Digest)
            ? Digest
            : FString();
    };
    ON_SCOPE_EXIT
    {
        Receipt->SetArrayField(TEXT("cases"), Cases);
        Receipt->SetBoolField(TEXT("passed"), !HasAnyErrors());
        FString Text;
        FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<TCHAR>::Create(&Text));
        const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/RetainedVectorLegacy/result.json");
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        if (!FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            AddError(TEXT("Vector legacy receipt could not be saved."));
    };
    for (const FString& Package : Names)
    {
        auto Source = MakeShared<FP4KadanSourceAsset>();
        Source->Package = Package;
        Source->Kind = TEXT("VectorFieldStatic");
        Source->Header = SourceRoot / (Package.Mid(6) + TEXT(".uasset"));
        Source->Body = FPaths::ChangeExtension(Source->Header, TEXT("uexp"));
        Sources.Add(Source);
        FString Local;
        FPackageName::TryConvertLongPackageNameToFilename(Package, Local, TEXT(".uasset"));
        const FString Original = SavedRoot / (Package.Mid(6) + TEXT(".uasset"));
        const FString Before = FileDigest(Original);
        if (!TestTrue(TEXT("The test starts with actual retained copies, not fresh native substitutes"),
                !Before.IsEmpty() && FileDigest(Local) == Before))
            return false;
        FixtureDigests.Add(Original, Before);
    }
    // Native predicate boundary: no malformed bytes, flags, count or pointer can
    // become permission to replace an existing original.
    TArray<uint8> Empty = {0, 0, 0, 0, 0x48, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 24, 0, 0, 0, 0, 0, 0, 0};
    FP4VectorFieldTail Read;
    FString Error;
    TestTrue(TEXT("The precise canonical legacy empty header qualifies"),
        P4LegacyVectorField::EmptyTail(Empty.GetData(), Empty.Num(), 24, Read, Error));
    for (int32 Offset : {0, 4, 8, 12, 16})
    {
        TArray<uint8> Bad = Empty;
        Bad[Offset] ^= 1;
        TestFalse(TEXT("GUID, flags, element/disk counts and inline offset edits all refuse"),
            P4LegacyVectorField::EmptyTail(Bad.GetData(), Bad.Num(), 24, Read, Error));
        TestTrue(TEXT("Refused tail input remains unchanged"), Bad[Offset] == uint8(Empty[Offset] ^ 1));
    }
    TArray<uint8> OtherFlags = Empty;
    OtherFlags[4] = 0x40;
    TestTrue(TEXT("Other inline flags are readable native data"),
        P4ReadVectorFieldTail(OtherFlags.GetData(), OtherFlags.Num(), Read, Error));
    TestFalse(TEXT("Readable alternative flags still are not the canonical legacy header"),
        P4LegacyVectorField::EmptyTail(OtherFlags.GetData(), OtherFlags.Num(), 24, Read, Error));
    TArray<uint8> Extra = Empty;
    Extra.Add(0);
    TestFalse(TEXT("A trailing payload cannot qualify by prefix"),
        P4LegacyVectorField::EmptyTail(Extra.GetData(), Extra.Num(), 24, Read, Error));
    TArray<uint8> Short = Empty;
    Short.Pop();
    TestFalse(TEXT("A truncated header refuses"),
        P4LegacyVectorField::EmptyTail(Short.GetData(), Short.Num(), 24, Read, Error));
    {
        FP4KadanImportSourceScope Scope(Sources);
        for (const auto& Source : Sources)
        {
            TStrongObjectPtr<UVectorFieldStatic> Field(
                LoadObject<UVectorFieldStatic>(nullptr, *ObjectPath(Source->Package)));
            if (!TestTrue(TEXT("The retained vector is loaded with no native source payload"),
                    Field.IsValid() && Field->SourceData.GetBulkDataSize() == 0))
                return false;
            FP4OriginalRecord Record;
            FString Why;
            if (!TestTrue(TEXT("The original v1 record remains present and qualified"),
                    P4ReadOriginalRecord(Field.Get(), Record, Why) && Why.IsEmpty()))
                return false;
            OldDigests.Add(Source->Package, Record.ComparisonDigest);
            UMetaData* Meta = Field->GetOutermost()->GetMetaData();
            const FString Marker = Meta->GetValue(Field.Get(), TEXT("P4OriginalRecord"));
            const FString Baseline = Meta->GetValue(Field.Get(), TEXT("P4NativeObjectBaseline"));
            const bool HadNotHeld = Meta->HasValue(Field.Get(), TEXT("P4NativeObjectValuesNotHeld"));
            const FString NotHeld = Meta->GetValue(Field.Get(), TEXT("P4NativeObjectValuesNotHeld"));
            const auto Positive = P4DiagnoseOriginalAsset(Field.Get());
            if (!TestTrue(TEXT("The actual current native cook reproduces exactly the prior recorded v1 digest"),
                    Positive.Error.IsEmpty() && Positive.Comparison == EP4OriginalComparison::Equal &&
                        Positive.CurrentDigest == Record.ComparisonDigest))
            {
                AddError(Positive.Error);
                return false;
            }
            auto Case = MakeShared<FJsonObject>();
            Case->SetStringField(TEXT("package"), Source->Package);
            Case->SetStringField(TEXT("old_recorded_digest"), Record.ComparisonDigest);
            Case->SetStringField(TEXT("actual_current_legacy_digest"), Positive.CurrentDigest);
            Cases.Add(MakeShared<FJsonValueObject>(Case));
            const auto Refuses = [&]()
            {
                const auto D = P4DiagnoseOriginalAsset(Field.Get());
                return D.Comparison != EP4OriginalComparison::Equal;
            };
            {
                // A still-readable package with changed current bytes is not the
                // source named by the retained record. Never edit the real pair.
                TArray<uint8> Head, Body;
                if (!FFileHelper::LoadFileToArray(Head, *Source->Header) ||
                    !FFileHelper::LoadFileToArray(Body, *Source->Body))
                    return false;
                Body.Add(0);
                const FString Base = FPaths::ProjectSavedDir() / TEXT("P4MapKit/RetainedVectorLegacy/Tamper") /
                    FGuid::NewGuid().ToString(EGuidFormats::Digits);
                const FString Header = Base + TEXT(".uasset"), BodyFile = Base + TEXT(".uexp");
                IFileManager::Get().MakeDirectory(*FPaths::GetPath(Base), true);
                ON_SCOPE_EXIT
                {
                    IFileManager::Get().Delete(*Header);
                    IFileManager::Get().Delete(*BodyFile);
                };
                if (!FFileHelper::SaveArrayToFile(Head, *Header) || !FFileHelper::SaveArrayToFile(Body, *BodyFile))
                    return false;
                auto Changed = MakeShared<FP4KadanSourceAsset>(*Source);
                Changed->Header = Header;
                Changed->Body = BodyFile;
                FP4KadanImportSourceScope ChangedScope({Changed});
                TestTrue(TEXT("Actual current source-byte tampering refuses before retirement"), Refuses());
            }
            const int32 Size = Field->SizeX;
            Field->SizeX = Size + 1;
            TestTrue(TEXT("A current dimensions edit cannot be discarded"), Refuses());
            Field->SizeX = Size;
            const FBox Bounds = Field->Bounds;
            Field->Bounds.Min.X += 1;
            TestTrue(TEXT("A current Box edit cannot be discarded"), Refuses());
            Field->Bounds = Bounds;
            Field->SourceData.Lock(LOCK_READ_WRITE);
            void* Data = Field->SourceData.Realloc(8);
            FMemory::Memzero(Data, 8);
            Field->SourceData.Unlock();
            TestTrue(TEXT("The single-use negative retains a nonzero count without a reloadable allocation"),
                Field->SourceData.GetElementCount() == 8 && Field->SourceData.GetBulkDataSize() == 8 &&
                    !Field->SourceData.IsBulkDataLoaded());
            const auto Nonempty = P4DiagnoseOriginalAsset(Field.Get());
            TestTrue(TEXT("Nonempty current native payload refuses before a cook or generic fallback"),
                Nonempty.Comparison == EP4OriginalComparison::Unreadable &&
                    Nonempty.Error == TEXT("legacy-vector-field-current-bulk-not-empty") &&
                    Nonempty.CurrentDigest.IsEmpty() && Field->SourceData.GetElementCount() == 8 &&
                    Field->SourceData.GetBulkDataSize() == 8 && !Field->SourceData.IsBulkDataLoaded());
            // Lock would try reloading the discarded single-use allocation.
            // Reset only this test's injected payload without touching its flags.
            Field->SourceData.RemoveBulkData();
            TestTrue(TEXT("Removing the control restores empty native counts without loading discarded bytes"),
                Field->SourceData.GetElementCount() == 0 && Field->SourceData.GetBulkDataSize() == 0);
            const auto RecordEdit = [&](const TCHAR* Key, const FString& Value)
            {
                TSharedPtr<FJsonObject> Json;
                FString Text;
                if (!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Marker), Json) || !Json.IsValid())
                    return false;
                Json->SetStringField(Key, Value);
                if (!FJsonSerializer::Serialize(
                        Json.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)))
                    return false;
                Meta->SetValue(Field.Get(), TEXT("P4OriginalRecord"), *Text);
                const bool Rejected = Refuses();
                Meta->SetValue(Field.Get(), TEXT("P4OriginalRecord"), *Marker);
                return Rejected;
            };
            TestTrue(TEXT("A wrong baseline digest refuses"),
                RecordEdit(TEXT("comparison_digest"), FString::ChrN(64, TCHAR('0'))));
            TestTrue(TEXT("A wrong source digest refuses"),
                RecordEdit(TEXT("source_digest"), FString::ChrN(64, TCHAR('0'))));
            TestTrue(TEXT("A different schema refuses"),
                RecordEdit(TEXT("comparison_schema"), TEXT("object-lines-unknown")));
            TestTrue(TEXT("A case-varied schema does not enter the exact legacy path"),
                RecordEdit(TEXT("comparison_schema"), TEXT("OBJECT-lines-v1")));
            {
                TSharedPtr<FJsonObject> Json;
                FString Text;
                if (!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Marker), Json) || !Json.IsValid())
                    return false;
                Json->RemoveField(TEXT("values"));
                if (!FJsonSerializer::Serialize(
                        Json.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)))
                    return false;
                Meta->SetValue(Field.Get(), TEXT("P4OriginalRecord"), *Text);
                TestTrue(TEXT("A missing values ledger is not proof of an empty ledger"), Refuses());
                Meta->SetValue(Field.Get(), TEXT("P4OriginalRecord"), *Marker);
            }
            Meta->RemoveValue(Field.Get(), TEXT("P4OriginalRecord"));
            TestTrue(TEXT("A missing v2 marker never becomes implicit legacy edit permission"), Refuses());
            Meta->SetValue(Field.Get(), TEXT("P4OriginalRecord"), *Marker);
            Meta->RemoveValue(Field.Get(), TEXT("P4NativeObjectBaseline"));
            TestTrue(TEXT("Missing legacy evidence refuses"), Refuses());
            Meta->SetValue(Field.Get(), TEXT("P4NativeObjectBaseline"), *Baseline);
            Meta->SetValue(
                Field.Get(), TEXT("P4NativeObjectValuesNotHeld"), TEXT("unexpected unsupported native value"));
            TestTrue(TEXT("An unsupported-value note cannot be hidden by an empty record ledger"), Refuses());
            if (HadNotHeld)
                Meta->SetValue(Field.Get(), TEXT("P4NativeObjectValuesNotHeld"), *NotHeld);
            else
                Meta->RemoveValue(Field.Get(), TEXT("P4NativeObjectValuesNotHeld"));
            TestTrue(TEXT("The negative restores exact unsupported-note presence and content"),
                HadNotHeld == Meta->HasValue(Field.Get(), TEXT("P4NativeObjectValuesNotHeld")) &&
                    P4ExactNativeString(NotHeld, Meta->GetValue(Field.Get(), TEXT("P4NativeObjectValuesNotHeld"))));
            const bool WasDirty = Field->GetOutermost()->IsDirty();
            Field->GetOutermost()->SetDirtyFlag(true);
            TestTrue(TEXT("Dirty creator content refuses"), Refuses());
            Field->GetOutermost()->SetDirtyFlag(WasDirty);
            TestTrue(TEXT("All controls preserve the original baseline and exact marker text"),
                Marker == FString(Meta->GetValue(Field.Get(), TEXT("P4OriginalRecord"))) &&
                    Baseline == FString(Meta->GetValue(Field.Get(), TEXT("P4NativeObjectBaseline"))));
            const auto Restored = P4DiagnoseOriginalAsset(Field.Get());
            TestTrue(TEXT("Restoring controls restores the same existing digest, not a new baseline"),
                Restored.Error.IsEmpty() && Restored.CurrentDigest == Record.ComparisonDigest &&
                    Restored.Comparison == EP4OriginalComparison::Equal);
        }
    }
    // Unbound UI calls cannot manufacture a current source witness.
    for (const FString& Package : Names)
        TestTrue(TEXT("A missing current source binding refuses compatibility"),
            P4DiagnoseOriginalAsset(LoadObject<UObject>(nullptr, *ObjectPath(Package))).Comparison !=
                EP4OriginalComparison::Equal);
    if (HasAnyErrors())
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = SourceRoot;
    const TSet<FString> Roots = {Names[0], Names[1]};
    const auto Result = P4KadanRunImport(Settings, P4KadanPlanImport(Sources, Roots, false), false);
    if (!TestTrue(TEXT("Both retained vectors pass the ordinary source fill and actual cold Finish"),
            Result.Ok && !Result.NotImported.Num()))
    {
        for (const FString& Problem : Result.Problems)
            AddError(Problem);
        return false;
    }
    TSharedPtr<FJsonObject> Report;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Result.Report), Report) || !Report.IsValid())
        return false;
    const TSharedPtr<FJsonObject>* Assets = nullptr;
    if (!Report->TryGetObjectField(TEXT("assets"), Assets))
        return false;
    for (const FString& Package : Names)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        if (!(*Assets)->TryGetObjectField(Package, Row))
        {
            AddError(TEXT("A retained vector has no actual reply"));
            return false;
        }
        bool Valid = false, Saved = false, Reference = true;
        (*Row)->TryGetBoolField(TEXT("valid"), Valid);
        (*Row)->TryGetBoolField(TEXT("saved"), Saved);
        (*Row)->TryGetBoolField(TEXT("reference_only"), Reference);
        UVectorFieldStatic* Field = LoadObject<UVectorFieldStatic>(nullptr, *ObjectPath(Package));
        const auto Cold = P4DiagnoseOriginalAsset(Field);
        TestTrue(TEXT("Full source bulk and a strict new non-reference record survive the native cold reload"),
            Valid && Saved && !Reference && Field &&
                Field->SourceData.GetBulkDataSize() == int64(Field->SizeX) * Field->SizeY * Field->SizeZ * 8 &&
                Field->SourceData.GetBulkDataSize() > 0 && Cold.Error.IsEmpty() &&
                Cold.Comparison == EP4OriginalComparison::Equal && !Cold.Provenance.Record.ReferenceOnly &&
                Cold.CurrentDigest != OldDigests.FindChecked(Package));
    }
    for (const auto& Pair : FixtureDigests)
        TestEqual(
            TEXT("The retained original source fixture file stays byte-identical"), FileDigest(Pair.Key), Pair.Value);
    return !HasAnyErrors();
}
#endif

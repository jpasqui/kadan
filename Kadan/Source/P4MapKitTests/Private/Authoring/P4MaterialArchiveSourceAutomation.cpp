#include "P4KadanSourceAcquire.h"
#include "P4KadanImport.h"
#include "P4KadanSourceCatalog.h"
#include "P4OriginalRecord.h"
#include "P4KadanImportSources.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "Serialization/Archive.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "PackageTools.h"
#include "AssetRegistryModule.h"

namespace P4MaterialArchiveSourceTest
{
bool Digest(const FString& File, FString& Out)
{
    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*File));
    if (!Reader || Reader->TotalSize() <= 0 || Reader->TotalSize() > (int64(1) << 31))
        return false;
    FSHA1 Hash;
    TArray<uint8> Block;
    Block.SetNumUninitialized(65536);
    while (Reader->Tell() < Reader->TotalSize())
    {
        const int64 Count = FMath::Min<int64>(Block.Num(), Reader->TotalSize() - Reader->Tell());
        Reader->Serialize(Block.GetData(), Count);
        if (Reader->IsError())
            return false;
        Hash.Update(Block.GetData(), uint32(Count));
    }
    Hash.Final();
    uint8 Bytes[20];
    Hash.GetHash(Bytes);
    Out = BytesToHex(Bytes, 20);
    return true;
}
bool RetireOwnFailedMaster(const FP4KadanImportPlan& Plan, const FString& Package, const FString& Proof, FString& Error)
{
    FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeDirectoryName(Project);
    if (!Project.EndsWith(TEXT("/private/native-development/project"), ESearchCase::IgnoreCase))
    {
        Error = TEXT("Failed-master retry is restricted to the owned native-development fixture.");
        return false;
    }
    UMaterial* Material = LoadObject<UMaterial>(nullptr, *(Package + TEXT(".M_demo_Black_Master")));
    FP4OriginalRecord Record;
    if (!Material || Material->Expressions.Num() || Material->GetOutermost()->IsDirty() ||
        !P4ReadOriginalRecord(Material, Record, Error) || !Record.ReferenceOnly || !Record.ReadOnly ||
        Record.Kind != TEXT("object") || FName(*Record.Package) != FName(*Package))
    {
        Error = TEXT("Retry requires the clean graphless original created by the failed material regression.");
        return false;
    }
    FString PriorText;
    TSharedPtr<FJsonObject> Prior;
    const TSharedPtr<FJsonObject>* Assets = nullptr;
    const TSharedPtr<FJsonObject>* Row = nullptr;
    bool Created = false;
    FString Why, Header;
    if (!FFileHelper::LoadFileToString(PriorText, *(FPaths::ProjectSavedDir() / TEXT("Kadan/import-result.json"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PriorText), Prior) || !Prior.IsValid() ||
        !Prior->TryGetObjectField(TEXT("assets"), Assets) || !(*Assets)->TryGetObjectField(Record.Package, Row) ||
        !(*Row)->TryGetBoolField(TEXT("created"), Created) || !Created ||
        !(*Row)->TryGetStringField(TEXT("recovered_graph_error"), Why) ||
        Why != TEXT("The game containers did not provide their Switch shader archive.") ||
        !(*Row)->TryGetStringField(TEXT("header"), Header))
    {
        Error = TEXT("The exact failed-creation receipt does not authorize retry.");
        return false;
    }
    FP4KadanImportSourceScope Scope(Plan.Scanned.Num() ? Plan.Scanned : Plan.Assets);
    FP4KadanSourceBinding Source;
    if (!P4KadanReadBoundSource(Record.Package, Source, Error) || Source.SourceDigest != Record.SourceDigest ||
        !P4KadanSourceBindingCurrent(Source, Error) ||
        P4DiagnoseOriginalAsset(Material).Comparison != EP4OriginalComparison::Equal)
    {
        Error = TEXT("The failed original's source/baseline no longer matches.");
        return false;
    }
    // RequestedOriginals may have been hard-linked from ConvertedOriginals.
    // Bind the exact prior receipt path too; different paths must have identical bytes.
    auto PriorSource = MakeShared<FP4KadanSourceAsset>();
    PriorSource->Package = Record.Package;
    PriorSource->Kind = TEXT("Material");
    PriorSource->Header = Header;
    PriorSource->Body = FPaths::ChangeExtension(Header, TEXT("uexp"));
    {
        FP4KadanImportSourceScope PriorScope({PriorSource});
        FP4KadanSourceBinding PriorBinding;
        if (!P4KadanReadBoundSource(Record.Package, PriorBinding, Error) ||
            PriorBinding.SourceDigest != Source.SourceDigest)
        {
            Error = TEXT("The prior failed-creation source no longer matches the selected source.");
            return false;
        }
    }
    const FString File = FPackageName::LongPackageNameToFilename(
        Record.Package, FPackageName::GetAssetPackageExtension());
    const FString Retired = Proof / TEXT("FailedDemoBlack");
    IFileManager::Get().MakeDirectory(*Retired, true);
    TArray<TPair<FString, FString>> Files;
    TMap<FString, FString> FileHashes;
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const TCHAR* Extension : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
    {
        const FString From = FPaths::ChangeExtension(File, Extension), To = Retired / FPaths::GetCleanFilename(From);
        if (!IFileManager::Get().FileExists(*From))
            continue;
        FString Hash;
        if (IFileManager::Get().FileExists(*To) || !Digest(From, Hash))
        {
            Error = TEXT("Failed-master retirement destination is occupied or input unreadable.");
            return false;
        }
        Files.Emplace(From, To);
        FileHashes.Add(From, Hash);
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("from"), From);
        Item->SetStringField(TEXT("to"), To);
        Item->SetStringField(TEXT("sha1"), Hash);
        Rows.Add(MakeShared<FJsonValueObject>(Item));
    }
    auto Ledger = MakeShared<FJsonObject>();
    Ledger->SetStringField(TEXT("owner"), TEXT("native-material-fresh99"));
    Ledger->SetStringField(TEXT("package"), Record.Package);
    Ledger->SetStringField(TEXT("current_source_sha256"), Source.SourceDigest);
    Ledger->SetArrayField(TEXT("files"), Rows);
    FString Text;
    FJsonSerializer::Serialize(Ledger, TJsonWriterFactory<>::Create(&Text));
    if (!Files.Num() ||
        !FFileHelper::SaveStringToFile(Text, *(Proof / TEXT("failed-material-retirement.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !P4KadanSourceBindingCurrent(Source, Error))
    {
        Error = TEXT("The owned failed material could not be retired with its evidence.");
        return false;
    }
    const FString ObjectPath = Material->GetPathName();
    UPackage* OwnedPackage = Material->GetOutermost();
    FAssetRegistryModule::AssetDeleted(Material);
    Material = nullptr;
    FText UnloadError;
    if (!UPackageTools::UnloadPackages({OwnedPackage}, UnloadError))
    {
        Error = UnloadError.ToString();
        return false;
    }
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const bool LivePackage = FindPackage(nullptr, *Record.Package) != nullptr;
    const bool LiveObject = FindObject<UObject>(nullptr, *ObjectPath) != nullptr;
    const bool RegistryIdentity = Registry.GetAssetByObjectPath(FName(*ObjectPath)).IsValid();
    // UE keeps disk catalogue entries after unloading. ExistingReal deliberately
    // requires a current package file before those entries can finish a resume.
    // The actual live package/object must be gone before any owned file moves.
    Ledger->SetBoolField(TEXT("disk_registry_entry_after_unload"), RegistryIdentity);
    Text.Reset();
    if (!FJsonSerializer::Serialize(Ledger, TJsonWriterFactory<>::Create(&Text)) ||
        !FFileHelper::SaveStringToFile(Text, *(Proof / TEXT("failed-material-retirement.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        Error = TEXT("The unload evidence could not be saved.");
        return false;
    }
    if (LivePackage || LiveObject)
    {
        Error = FString::Printf(
            TEXT("The failed master still has an identity after unload: package=%d object=%d registry=%d (%s)."),
            int32(LivePackage), int32(LiveObject), int32(RegistryIdentity), *ObjectPath);
        return false;
    }
    int32 Moved = 0;
    for (const auto& Pair : Files)
    {
        FString Current;
        if (!Digest(Pair.Key, Current) || Current != FileHashes[Pair.Key] ||
            !IFileManager::Get().Move(*Pair.Value, *Pair.Key, false, false, false, true))
        {
            while (Moved > 0)
            {
                --Moved;
                IFileManager::Get().Move(*Files[Moved].Key, *Files[Moved].Value, false, false, false, true);
            }
            Error = TEXT("The failed-material move did not complete; prior files were returned.");
            return false;
        }
        ++Moved;
    }
    if (FPackageName::DoesPackageExist(Record.Package) || FindPackage(nullptr, *Record.Package) ||
        FindObject<UObject>(nullptr, *ObjectPath))
    {
        Error = TEXT("The failed-master destination or live identity is still occupied.");
        return false;
    }
    return true;
}
} //namespace P4MaterialArchiveSourceTest
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialShaderArchiveCache, "P4MapKit.Kadan.MaterialShaderArchiveCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialShaderArchiveCache::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SHADER_ARCHIVE_PROOF")).IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Opt-in real Paks-only archive proof: P4_NATIVE_SHADER_ARCHIVE_PROOF, P4_NATIVE_ORIGINAL_CONTAINERS and P4_NATIVE_RETOC"));
        return true;
    }
    FP4KadanImportSettings Settings;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    const FString Proof = FPaths::ProjectSavedDir() / TEXT("P4MapKit/ShaderArchiveProof");
    IFileManager::Get().MakeDirectory(*Proof, true);
    Settings.ConvertedCache = P4KadanCatalogGameFolder();
    Settings.GameFolder = Settings.ConvertedCache;
    const FString Expected = FPaths::ConvertRelativePathToFull(
        Settings.ConvertedCache / TEXT("Carrot4/Content/ShaderArchive-Carrot4-GLSL_SWITCH.ushaderbytecode"));
    const bool Fresh = !IFileManager::Get().FileExists(*Expected);
    const FString Package = TEXT("/Game/Carrot4/MasterMaterial/Demo/M_demo_Black_Master");
    // This simple real master is absent from the retained Cave fixture. Its
    // ordinary Paks-only import must acquire the library itself, not a test pre-step.
    const auto Plan = P4KadanPlanImport(Settings, {Package}, true);
    for (const FString& Problem : Plan.Problems)
        AddError(Problem);
    if (HasAnyErrors())
        return false;
    FString RetryError;
    if (!FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETRY_OWNED_FAILED_MATERIAL")).IsEmpty() &&
        FPackageName::DoesPackageExist(Package) &&
        !P4MaterialArchiveSourceTest::RetireOwnFailedMaster(Plan, Package, Proof, RetryError))
    {
        AddError(RetryError);
        return false;
    }
    const bool FreshMaterial = !FPackageName::DoesPackageExist(Package);
    const auto Imported = P4KadanRunImport(Settings, Plan, true);
    if (!TestTrue(TEXT("Ordinary fresh material import completes"), Imported.Ok))
    {
        for (const FString& Problem : Imported.Problems)
            AddError(Problem);
        return false;
    }
    UMaterial* Material = LoadObject<UMaterial>(nullptr, *(Package + TEXT(".M_demo_Black_Master")));
    FP4OriginalRecord Record;
    FString RecordError;
    if (!TestTrue(TEXT("The real master has a recovered graph, not a graphless reference substitute"),
            Material && Material->Expressions.Num() > 0 && P4ReadOriginalRecord(Material, Record, RecordError) &&
                Record.Kind == TEXT("material") && Record.Fidelity == TEXT("recovered-pixel-contract") &&
                Record.SourceDigestKind == TEXT("material-graph-v2-md5")))
    {
        AddError(RecordError);
        return false;
    }
    FString Archive, Error;
    if (!TestTrue(TEXT("Actual selected containers provide the Switch library without any extracted materials"),
            P4KadanAcquireShaderArchive(Settings, Archive, Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(TEXT("The archive is in the fingerprinted owned cache"), FPaths::IsSamePath(Archive, Expected)))
        return false;
    FString Before;
    if (!TestTrue(
            TEXT("The real archive is completely readable"), P4MaterialArchiveSourceTest::Digest(Archive, Before)))
        return false;
    const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Archive);
    const int64 Size = IFileManager::Get().FileSize(*Archive);
    const FString MaterialFile = FPackageName::LongPackageNameToFilename(
        Package, FPackageName::GetAssetPackageExtension());
    const FDateTime MaterialStamp = IFileManager::Get().GetTimeStamp(*MaterialFile);
    FString MaterialBefore, MaterialAfter;
    if (!TestTrue(TEXT("Imported original material bundle is readable"),
            P4MaterialArchiveSourceTest::Digest(MaterialFile, MaterialBefore)))
        return false;
    const auto Second = P4KadanRunImport(Settings, P4KadanPlanImport(Settings, {Package}, true), true);
    TestTrue(TEXT("Second ordinary import preserves the completed material and library without rewriting"),
        Second.Ok && IFileManager::Get().GetTimeStamp(*MaterialFile) == MaterialStamp &&
            P4MaterialArchiveSourceTest::Digest(MaterialFile, MaterialAfter) && MaterialAfter == MaterialBefore &&
            IFileManager::Get().GetTimeStamp(*Archive) == Stamp);
    FString Again, After;
    if (!TestTrue(TEXT("Second acquisition reuses the validated library"),
            P4KadanAcquireShaderArchive(Settings, Again, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Reuse does not rewrite, truncate, or replace the library"),
        FPaths::IsSamePath(Archive, Again) && IFileManager::Get().GetTimeStamp(*Again) == Stamp &&
            IFileManager::Get().FileSize(*Again) == Size && P4MaterialArchiveSourceTest::Digest(Again, After) &&
            After == Before);
    const FString Invalid = Proof / TEXT("ExplicitInvalid.ushaderbytecode");
    if (!TestTrue(
            TEXT("Owned invalid-choice fixture is written"), FFileHelper::SaveStringToFile(TEXT("invalid"), *Invalid)))
        return false;
    Settings.ShaderArchive = Invalid;
    TestFalse(TEXT("An explicit invalid external library is refused instead of silently replaced"),
        P4KadanAcquireShaderArchive(Settings, Again, Error));
    TestTrue(TEXT("Explicit refusal leaves the valid cached library untouched"),
        IFileManager::Get().GetTimeStamp(*Archive) == Stamp && P4MaterialArchiveSourceTest::Digest(Archive, After) &&
            After == Before);
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("fresh_paks_only_acquisition"), Fresh);
    Report->SetBoolField(TEXT("fresh_material_destination"), FreshMaterial);
    Report->SetStringField(TEXT("material"), Package);
    Report->SetNumberField(TEXT("material_expressions"), Material->Expressions.Num());
    Report->SetBoolField(TEXT("second_call_unchanged"), !HasAnyErrors());
    Report->SetStringField(TEXT("archive"), Archive);
    Report->SetStringField(TEXT("archive_sha1"), Before);
    Report->SetNumberField(TEXT("archive_bytes"), double(Size));
    FString Text;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
    TestTrue(TEXT("Archive proof receipt is saved"),
        FFileHelper::SaveStringToFile(
            Text, *(Proof / TEXT("archive-proof.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    AddInfo(Fresh ? TEXT("Fresh Paks-only library extraction exercised")
                  : TEXT("Previously extracted owned library reused; this run is not new extraction evidence"));
    return !HasAnyErrors();
}
#endif

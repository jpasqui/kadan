// Actual retained particle sources are imported through the existing reference-only importer.
// This establishes typed game references, not particle payload/render/export fidelity.
#include "P4OriginalAssetImport.h"
#include "P4OriginalImportSave.h"
#include "P4OriginalRecord.h"
#include "P4PackageSave.h"
#include "P4LevelContent.h"
#include "P4KadanAssetPolicy.h"
#include "P4NativeHash.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/package_reader.h"
THIRD_PARTY_INCLUDES_END

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FRetainedAvatarReference
{
    FString Package, Header, Body, Digest;
};
bool RetainedReferenceBytes(const FRetainedAvatarReference& Row, TArray<uint8>& Header, TArray<uint8>& Body)
{
    FString Digest;
    for (const FString& File : {Row.Header, Row.Body})
        if (IFileManager::Get().FileSize(*File) < 0 || IFileManager::Get().FileSize(*File) > 64 * 1024 * 1024)
            return false;
    return FFileHelper::LoadFileToArray(Header, *Row.Header, FILEREAD_Silent) &&
        FFileHelper::LoadFileToArray(Body, *Row.Body, FILEREAD_Silent) &&
        P4HashSourceBuffers(Header.GetData(), Header.Num(), Body.GetData(), Body.Num(), Digest) &&
        P4ExactNativeString(Digest, Row.Digest);
}
} //namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RetainedAvatarReferencesTest,
    "P4MapKit.NativeImport.RepairRetainedAvatarParticleReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RetainedAvatarReferencesTest::RunTest(const FString&)
{
    const FString ManifestFile = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_AVATAR_REFERENCES"));
    FString Text, Format;
    double Version = 0;
    TSharedPtr<FJsonObject> Manifest;
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!TestTrue(TEXT("Only the explicit controlled KadanOne retained-source manifest is admitted"),
            FString(FApp::GetProjectName()) == TEXT("KadanOne") && !ManifestFile.IsEmpty() &&
                IFileManager::Get().FileSize(*ManifestFile) >= 0 &&
                IFileManager::Get().FileSize(*ManifestFile) <= 64 * 1024 &&
                FFileHelper::LoadFileToString(Text, *ManifestFile) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Manifest) && Manifest.IsValid() &&
                Manifest->TryGetStringField(TEXT("format"), Format) &&
                Format == TEXT("p4-retained-avatar-references") &&
                Manifest->TryGetNumberField(TEXT("version"), Version) && Version == 1 &&
                Manifest->TryGetArrayField(TEXT("rows"), Rows) && Rows->Num() == 2))
        return false;
    const TArray<FString> Packages = {TEXT("/Game/Carrot4/Effects/Player/Npc/PS_eft_Npc_SurvivorAlert00"),
        TEXT("/Game/Carrot4/Effects/Player/Npc/PS_eft_Npc_LeafLight00")};
    TArray<FRetainedAvatarReference> Prepared;
    for (int32 Index = 0; Index < Rows->Num(); ++Index)
    {
        const auto Row = (*Rows)[Index]->AsObject();
        FRetainedAvatarReference One;
        TArray<uint8> Header, Body;
        if (!TestTrue(TEXT("Both actual source pairs have their exact expected package and byte digest"),
                Row.IsValid() && Row->TryGetStringField(TEXT("package"), One.Package) &&
                    One.Package == Packages[Index] && Row->TryGetStringField(TEXT("header"), One.Header) &&
                    Row->TryGetStringField(TEXT("body"), One.Body) &&
                    Row->TryGetStringField(TEXT("source_sha256"), One.Digest) && One.Digest.Len() == 64 &&
                    RetainedReferenceBytes(One, Header, Body)))
            return false;
        const std::vector<std::uint8_t> H(Header.GetData(), Header.GetData() + Header.Num()),
            B(Body.GetData(), Body.GetData() + Body.Num());
        const auto Source = P4MapKit::ReadPackage(H, B);
        int32 Top = 0;
        std::string Kind;
        for (const auto& Export : Source.Exports)
            if (!Export.Outer)
            {
                ++Top;
                if (Export.Name != std::string(TCHAR_TO_UTF8(*FPackageName::GetShortName(One.Package))) ||
                    !Source.Reference(Export.Class, Kind))
                    return false;
            }
        if (!TestTrue(TEXT("The actual package has exactly its named ParticleSystem root, not a fabricated asset"),
                Source.Error.Code.empty() && Source.Package == std::string(TCHAR_TO_UTF8(*One.Package)) && Top == 1 &&
                    Kind == "/Script/Engine.ParticleSystem"))
            return false;
        Prepared.Add(MoveTemp(One));
    }
    P4ImportPublication::FImportScope Import;
    auto Receipt = MakeShared<FJsonObject>();
    const FString ReceiptFile = FPaths::ProjectSavedDir() / TEXT("retained-avatar-reference-imports.json");
    bool Good = true;
    for (const auto& One : Prepared)
    {
        TArray<uint8> Header, Body;
        if (!TestTrue(
                TEXT("The source bytes have not changed before import"), RetainedReferenceBytes(One, Header, Body)))
        {
            Good = false;
            break;
        }
        // Source digest is checked again on the import result BEFORE any package publication. The
        // ordinary importer rejects occupied/edited assets and records all unheld data as reference-only.
        auto Imported = P4ImportOriginalAssetNative(One.Header, One.Body, false, true);
        TStrongObjectPtr<UObject> Hold(Imported.Asset);
        FP4OriginalRecord Record;
        FString Error;
        const auto Policy = P4Kadan::GetAssetPolicy(Hold.Get());
        const bool Verified = Imported.IsValid() && Hold.IsValid() &&
            P4ExactNativeString(Imported.Package, One.Package) &&
            P4ExactNativeString(Imported.SourceDigest, One.Digest) &&
            Imported.Class == TEXT("/Script/Engine.ParticleSystem") && Imported.ReferenceOnly &&
            P4ReadOriginalRecord(Hold.Get(), Record, Error) && P4ExactNativeString(Record.SourceDigest, One.Digest) &&
            Record.ReferenceOnly && Record.ReadOnly && Policy.Original && Policy.ReferenceOnly && !Policy.AllowEdit &&
            !Policy.StandIn;
        if (!TestTrue(
                TEXT("The real source-backed typed object has protected reference-only provenance: ") + One.Package,
                Verified))
        {
            AddError(Imported.ErrorDetail.IsEmpty() ? Error : Imported.ErrorDetail);
            Good = false;
        }
        else
        {
            const FString Expected = Imported.State;
            Imported.Saved = P4SaveAssetPackage(
                Hold.Get(), [&]() { return P4ExactNativeString(P4OriginalObjectAssetState(Hold.Get()), Expected); },
                Error);
            if (!TestTrue(TEXT("Only the verified actual reference object is published"), Imported.Saved))
            {
                AddError(Error);
                Good = false;
            }
        }
        TSharedPtr<FJsonObject> Report;
        FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(P4OriginalAssetImportJson(Imported)), Report);
        if (Report.IsValid())
            Receipt->SetObjectField(One.Package, Report);
        if (!Good)
            break;
    }
    FString ReportText;
    FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<TCHAR>::Create(&ReportText));
    TestTrue(TEXT("Retain actual import/native-unheld-data receipts"),
        FFileHelper::SaveStringToFile(ReportText, *ReceiptFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    AddInfo(TEXT("Typed game references only; particle payload rendering/export recovery is not claimed. Receipt: ") +
        ReceiptFile);
    return Good && !HasAnyErrors();
}
#endif

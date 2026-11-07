#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialGraph.h"
#include "P4MaterialCandidateSourceProof.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "RHI.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialAnisotropyPinsTest, "P4MapKit.Kadan.MaterialAnisotropyPins",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialAnisotropyPinsTest::RunTest(const FString&)
{
    // The exact native source recovery of M_obj_Terrain_Anisotropy_Opaque_00_Master,
    // seven users/seven owners, 45 nodes. Only its native input mapping is under
    // test; this does not promote auxiliary source facts to game renderer parity.
    const FString SourceFile = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ANISOTROPY_GRAPH"));
    FString Hash, Source, Error;
    int64 Bytes = 0;
    if (!TestTrue(TEXT("Exact source-recovered anisotropy graph is required"),
            !SourceFile.IsEmpty() && P4HashFile(SourceFile, 1024 * 1024, Bytes, Hash) &&
                Hash == TEXT("0eb45294f35171473befd45a8a8dd4d641cba730c863b866092d7fbde030e43f") &&
                FFileHelper::LoadFileToString(Source, *SourceFile)))
        return false;
    if (!TestFalse(TEXT("Actual renderer is required for anisotropy mapping proof"), GUsingNullRHI))
        return false;
    TArray<TStrongObjectPtr<UMaterial>> Made;
    ON_SCOPE_EXIT
    {
        for (int32 I = Made.Num() - 1; I >= 0; --I)
            if (Made[I].IsValid())
            {
                UMaterial* Material = Made[I].Get();
                const FString Path = Material->GetPathName(), Package = Material->GetOutermost()->GetName();
                FAssetRegistryModule::AssetDeleted(Material);
                Material->ClearFlags(RF_Public | RF_Standalone);
                const bool Retired = Material->Rename(nullptr, GetTransientPackage(),
                    REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
                if (Retired)
                    Material->MarkPendingKill();
                TestTrue(TEXT("Only unsaved owned anisotropy candidate is retired"),
                    Retired && !FindObject<UObject>(nullptr, *Path) && !FPackageName::DoesPackageExist(Package));
            }
    };
    const auto Build = [&](const FString& Graph, const TCHAR* Name) -> UMaterial*
    {
        const FString Package = FString(TEXT("/Game/Carrot4/__KadanAnisotropyPins021/")) + Name;
        if (!TestTrue(TEXT("Only an unused unsaved test destination is admitted"),
                !FPackageName::DoesPackageExist(Package) && !FindPackage(nullptr, *Package)))
            return nullptr;
        UMaterial* Material = P4BuildMaterialFromGraph(Graph, Package, Error);
        if (Material)
            Made.Emplace(Material);
        else
            AddError(Error);
        return Material;
    };
    UMaterial* Material = Build(Source, TEXT("M_Source"));
    if (!TestNotNull(TEXT("Actual recovered anisotropy source graph builds"), Material))
        return false;
    if (!TestTrue(TEXT("Both recovered native scalar and vector inputs are connected"),
            Material->Anisotropy.Expression && Material->Tangent.Expression))
        return false;
    if (!TestTrue(TEXT("Native candidate exactly matches source input semantics"),
            P4FreshMaterialCandidateMatchesSource(Material, Source, Error)))
    {
        AddError(Error);
        return false;
    }
    FP4OriginalRecord Record;
    if (!TestTrue(TEXT("Native graph baseline is captured once and unchanged candidate compares Equal"),
            P4ReadOriginalRecord(Material, Record, Error) &&
                Record.ComparisonSchema == TEXT("material-native-state-v1") &&
                P4DiagnoseOriginalAsset(Material).Comparison == EP4OriginalComparison::Equal))
        return false;
    const FString Serialized = P4MaterialGraphJson(Material, Error);
    TSharedPtr<FJsonObject> Read;
    if (!TestTrue(TEXT("Native graph serialization retains both recovered input connections"),
            Error.IsEmpty() && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Serialized), Read) &&
                Read.IsValid() && Read->GetObjectField(TEXT("outputs"))->HasField(TEXT("Anisotropy")) &&
                Read->GetObjectField(TEXT("outputs"))->HasField(TEXT("Tangent"))))
        return false;
    const auto Anisotropy = Material->Anisotropy;
    const auto Tangent = Material->Tangent;
    Material->Anisotropy.Expression = nullptr;
    TestTrue(TEXT("Disconnecting actual scalar input changes strict native state"),
        P4DiagnoseOriginalAsset(Material).Comparison == EP4OriginalComparison::Different);
    TestFalse(TEXT("Disconnected scalar input fails exact source proof"),
        P4FreshMaterialCandidateMatchesSource(Material, Source, Error));
    Material->Anisotropy = Anisotropy;
    Material->Tangent.Expression = nullptr;
    TestTrue(TEXT("Disconnecting actual tangent input changes strict native state"),
        P4DiagnoseOriginalAsset(Material).Comparison == EP4OriginalComparison::Different);
    TestFalse(TEXT("Disconnected tangent input fails exact source proof"),
        P4FreshMaterialCandidateMatchesSource(Material, Source, Error));
    Material->Tangent = Tangent;
    for (const TCHAR* Pin : {TEXT("Anisotropy"), TEXT("Tangent")})
    {
        TSharedPtr<FJsonObject> Without;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source), Without) || !Without.IsValid())
            return false;
        Without->GetObjectField(TEXT("outputs"))->RemoveField(Pin);
        FString Omitted;
        if (!FJsonSerializer::Serialize(Without.ToSharedRef(), TJsonWriterFactory<>::Create(&Omitted)))
            return false;
        TestFalse(FString(TEXT("Unreported extra native connection fails source proof: ")) + Pin,
            P4FreshMaterialCandidateMatchesSource(Material, Omitted, Error));
        TestTrue(TEXT("The refusal identifies the exact extra input"),
            Error.Contains(FString(TEXT("extra-connected-pin:")) + Pin));
    }
    TestTrue(TEXT("Exact input restoration compares Equal without recapture"),
        P4DiagnoseOriginalAsset(Material).Comparison == EP4OriginalComparison::Equal);
    TestTrue(TEXT("Exact input restoration passes source proof"),
        P4FreshMaterialCandidateMatchesSource(Material, Source, Error));
    UMaterial* Roundtrip = Build(Serialized, TEXT("M_Roundtrip"));
    if (!TestNotNull(TEXT("Serialized graph reconstructs both native pins"), Roundtrip))
        return false;
    const bool Connected = Roundtrip->Anisotropy.Expression && Roundtrip->Tangent.Expression;
    FString ReadbackError;
    const FString RoundtripJson = P4MaterialGraphJson(Roundtrip, ReadbackError);
    const bool SameJson = P4ExactNativeString(RoundtripJson, Serialized);
    FString ProofError;
    const bool SourceMatches = P4FreshMaterialCandidateMatchesSource(Roundtrip, Source, ProofError);
    // Native readback can include an explicit output index. The recovered-source
    // proof accepts the original two-element links, not this serializer format.
    FString IndexedProofError;
    const bool IndexedSourceMatches = P4FreshMaterialCandidateMatchesSource(Roundtrip, Serialized, IndexedProofError);
    const bool IndexedSourceRefused = !IndexedSourceMatches &&
        IndexedProofError == TEXT("material-candidate-source-source-link-not-supported");
    TestTrue(TEXT("Second build retains both recovered native inputs"), Connected);
    TestTrue(TEXT("Second native graph serialization has no error"), ReadbackError.IsEmpty());
    TestTrue(TEXT("Native readback survives a second build exactly"), SameJson);
    TestTrue(TEXT("Second candidate passes exact original recovered-source proof"), SourceMatches);
    TestFalse(TEXT("Three-element native links do not widen recovered-source proof"), IndexedSourceMatches);
    TestTrue(TEXT("Three-element source links retain their explicit unsupported refusal"), IndexedSourceRefused);
    if (!Connected || !SameJson || !ReadbackError.IsEmpty() || !SourceMatches || !IndexedSourceRefused)
    {
        AddInfo(FString::Printf(
            TEXT(
                "Anisotropy roundtrip diagnostic: inputs=%d exact_json=%d source_proof=%d indexed_source_refused=%d readback_error=%s proof_error=%s indexed_proof_error=%s"),
            int32(Connected), int32(SameJson), int32(SourceMatches), int32(IndexedSourceRefused), *ReadbackError,
            *ProofError, *IndexedProofError));
        // Keep the exact comparisons above. These bounded native readbacks expose
        // the first differing value/connection instead of hiding it in an AND.
        if (Serialized.Len() <= 65536 && RoundtripJson.Len() <= 65536)
        {
            AddInfo(TEXT("Anisotropy native source JSON: ") + Serialized);
            AddInfo(TEXT("Anisotropy native roundtrip JSON: ") + RoundtripJson);
        }
        else
            AddInfo(FString::Printf(TEXT("Native graph diagnostic bound exceeded: source=%d roundtrip=%d"),
                Serialized.Len(), RoundtripJson.Len()));
    }
    for (UMaterial* Candidate : {Material, Roundtrip})
    {
        TArray<FString> CompileErrors;
        if (!TestTrue(TEXT("Actual anisotropy graph has a valid shader map and zero errors"),
                P4WaitForMaterialShaders(Candidate, CompileErrors, Error) && CompileErrors.Num() == 0))
        {
            AddError(Error + TEXT(" ") + FString::Join(CompileErrors, TEXT("; ")));
            return false;
        }
        TestTrue(TEXT("Compiled candidate remains Equal to its unchanged native baseline"),
            P4DiagnoseOriginalAsset(Candidate).Comparison == EP4OriginalComparison::Equal);
    }
    AddInfo(TEXT("Exact source graph SHA256: ") + Hash +
        TEXT("; seven source users, seven owners, 45 nodes; native Build/serialize/Equal/shader map only."));
    return !HasAnyErrors();
}
#endif

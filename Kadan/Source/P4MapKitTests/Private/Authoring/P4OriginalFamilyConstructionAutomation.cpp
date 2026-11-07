#include "CoreMinimal.h"
#include "P4KadanTestSeams.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "P4OriginalLevelImport.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "P4KadanImport.h"
#include "P4KadanImportSources.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalFamilyConstruction,
    "P4MapKit.NativeImport.ActualCaveRoomFamilyConstruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalFamilyConstruction::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_FAMILY_PROOF"));
    if (Header.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Opt-in source-family proof: set P4_NATIVE_FAMILY_PROOF to retained Cave000_F00_GameArea.umap"));
        return true;
    }
    const FString Body = FPaths::ChangeExtension(Header, TEXT("uexp"));
    TArray<uint8> H, B;
    FString Before;
    if (!TestTrue(TEXT("Actual retained original source is available and hashed"),
            FFileHelper::LoadFileToArray(H, *Header) && FFileHelper::LoadFileToArray(B, *Body) &&
                P4HashSourceBuffers(H.GetData(), H.Num(), B.GetData(), B.Num(), Before)))
        return false;
    const FString ClassRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_FAMILY_CLASS_SOURCES"));
    TArray<TSharedPtr<FP4KadanSourceAsset>> ClassSources;
    TArray<FString> SourceProblems;
    if (!ClassRoot.IsEmpty())
    {
        const bool Scanned = P4KadanScanGame(ClassRoot, ClassSources, SourceProblems);
        AddInfo(FString::Printf(TEXT("Class source registry root %s; %d source rows; %d scan problems"), *ClassRoot,
            ClassSources.Num(), SourceProblems.Num()));
        for (int32 Index = 0; Index < FMath::Min(32, SourceProblems.Num()); ++Index)
            AddInfo(TEXT("Class source scan: ") + SourceProblems[Index]);
        if (!TestTrue(TEXT("Explicit authoritative class-source registry is complete and unambiguous"),
                Scanned && SourceProblems.Num() == 0))
            return false;
    }
    TUniquePtr<FP4KadanImportSourceScope> SourceScope;
    if (!ClassRoot.IsEmpty())
        SourceScope = MakeUnique<FP4KadanImportSourceScope>(ClassSources);
    const auto Result = P4ProbeOriginalGameBackedFamiliesForTest(Header, Body);
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/FamilyProof");
    IFileManager::Get().MakeDirectory(*Folder, true);
    TestTrue(TEXT("The bounded source-family diagnostic receipt is saved"),
        FFileHelper::SaveStringToFile(P4OriginalLevelImportJson(Result), *(Folder / TEXT("cave-room-families.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    UWorld* World = Cast<UWorld>(Result.Asset);
    TStrongObjectPtr<UWorld> Hold(World);
    ON_SCOPE_EXIT
    {
        Hold.Reset();
        P4DiscardOriginalFamilyProbeForTest(World);
    };
    if (!TestTrue(TEXT("Actual source+settled constructor family proof runs without replacing the imported map: ") +
                Result.ErrorDetail,
            Result.IsValid() && World))
        return false;
    TestFalse(TEXT("A diagnostic cannot publish a saved reconstructed original"),
        Result.Saved || Result.BaselineKnown || Result.Created);
    FP4OriginalRecord Record;
    FString Error;
    TestFalse(TEXT("Diagnostic staging has no forged original marker"), P4ReadOriginalRecord(World, Record, Error));
    TestTrue(TEXT("The proof uses a distinct temporary package"),
        World->GetOutermost()->GetName().StartsWith(TEXT("/Temp/P4OriginalFamilyProof_")));
    TSet<FString> Kinds;
    for (AActor* Actor : World->PersistentLevel->Actors)
        if (IsValid(Actor))
            for (const TCHAR* Kind : {TEXT("RM"), TEXT("CF"), TEXT("CP"), TEXT("SL"), TEXT("WL"), TEXT("WY")})
                if (Actor->GetName().Contains(FString(TEXT("_")) + Kind + TEXT("_")))
                    Kinds.Add(Kind);
    for (const TCHAR* Kind : {TEXT("RM"), TEXT("CF"), TEXT("CP"), TEXT("SL"), TEXT("WL"), TEXT("WY")})
        TestTrue(FString(TEXT("Real source-qualified ")) + Kind +
                TEXT(" geometry actors survive exact cooking and parent construction replay"),
            Kinds.Contains(Kind));
    FString After;
    TestTrue(TEXT("Actual retained source bytes were not rewritten by the diagnostic"),
        FFileHelper::LoadFileToArray(H, *Header) && FFileHelper::LoadFileToArray(B, *Body) &&
            P4HashSourceBuffers(H.GetData(), H.Num(), B.GetData(), B.Num(), After) && After == Before);
    AddInfo(TEXT("Exact family refusal evidence (no baseline waiver): ") + Folder);
    return !HasAnyErrors();
}
#endif

#include "P4KadanImport.h"
#include "P4OriginalRecord.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalParticleResourcesTest, "P4MapKit.Particles.ActualResourcesPrep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalParticleResourcesTest::RunTest(const FString&)
{
    const FString Source = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTICLE_SOURCE"));
    if (Source.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "ActualResourcesPrep not exercised: set P4_NATIVE_PARTICLE_SOURCE in the owned fixture project to the retained ConvertedOriginals Content root."));
        return true;
    }
    const TSet<FString> Roots{TEXT("/Game/Carrot4/Effects/Player/Npc/PS_eft_Npc_LeafLight00"),
        TEXT("/Game/Carrot4/Effects/Player/Npc/PS_eft_Npc_SurvivorAlert00"),
        TEXT("/Game/Carrot4/Effects/Player/PS_eft_Player_ReturnOut00")};
    // Refuse occupied particle destinations up front. Preparation imports only their resources;
    // strict actual particle creation remains a separate source/cook-qualified gate.
    for (const FString& Package : Roots)
        if (!TestTrue(TEXT("Actual particle destination is still free before resource preparation: ") + Package,
                !FindPackage(nullptr, *Package) && !FPackageName::DoesPackageExist(Package)))
            return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Source;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    Settings.ShaderArchive = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SHADER_ARCHIVE"));
    Settings.AllowGameBackedLevels = false;
    FString NativeRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    FPaths::NormalizeFilename(NativeRoot);
    if (Settings.Retoc.IsEmpty() && !NativeRoot.IsEmpty())
        Settings.Retoc = FPaths::GetPath(FPaths::GetPath(NativeRoot)) / TEXT("private/retoc/retoc.exe");
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Settings.Repak.IsEmpty() && !Settings.Retoc.IsEmpty())
        Settings.Repak = FPaths::GetPath(FPaths::GetPath(Settings.Retoc)) / TEXT("repak/repak.exe");
    // Preserve the caller's already validated source rows and established normal dependency order.
    // Resume reuses existing real assets and source pairs; it does not reimport the whole source cache.
    FP4KadanImportPlan Plan = P4KadanPlanImport(Settings, Roots, true);
    for (const FString& Problem : Plan.Problems)
        AddError(Problem);
    if (!TestTrue(TEXT("The exact original particle resource closure is planned from real retained sources"),
            Plan.Problems.Num() == 0))
        return false;
    int32 ParticlesRemoved = 0;
    Plan.Assets.RemoveAll(
        [&](const TSharedPtr<FP4KadanSourceAsset>& Asset)
        {
            if (!Asset.IsValid() || !Roots.Contains(Asset->Package))
                return false;
            if (Asset->Kind != TEXT("ParticleSystem"))
            {
                AddError(Asset->Package + TEXT(" source kind is not ParticleSystem"));
                return false;
            }
            ++ParticlesRemoved;
            return true;
        });
    if (!TestEqual(TEXT("Only the three actual particle roots are deferred to their strict gate"), ParticlesRemoved, 3))
        return false;
    if (!TestTrue(TEXT("Preparation stays within the bounded actual resource closure"),
            Plan.Assets.Num() > 0 && Plan.Assets.Num() <= 128))
        return false;
    for (const auto& Asset : Plan.Assets)
        if (!TestTrue(TEXT("Every resource has actual source files and no World/Blueprint/particle reconstruction"),
                Asset.IsValid() && !Asset->Header.IsEmpty() && !Asset->Body.IsEmpty() &&
                    IFileManager::Get().FileExists(*Asset->Header) && IFileManager::Get().FileExists(*Asset->Body) &&
                    Asset->Kind != TEXT("ParticleSystem") && Asset->Kind != TEXT("World") &&
                    !Asset->Kind.Contains(TEXT("Blueprint"))))
            return false;
    const FP4KadanImportResult Imported = P4KadanRunImport(Settings, Plan, true);
    const FString ReceiptDirectory = FPaths::ProjectSavedDir() / TEXT("P4MapKit/Particles");
    TestTrue(TEXT("The resource preparation receipt is retained in one reusable output folder"),
        IFileManager::Get().MakeDirectory(*ReceiptDirectory, true) &&
            FFileHelper::SaveStringToFile(Imported.Report, *(ReceiptDirectory / TEXT("resources-prep.json")),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    for (const FString& Problem : Imported.Problems)
        AddError(Problem);
    for (const FString& Warning : Imported.Warnings)
        AddInfo(Warning);
    if (!TestTrue(TEXT("Normal resource import completed without cancellation"), Imported.Ok && !Imported.Cancelled))
        return false;
    int32 TypedResources = 0;
    for (const auto& Asset : Plan.Assets)
    {
        const FString Path = Asset->Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Asset->Package);
        UObject* Live = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        UClass* Expected = FindObject<UClass>(nullptr, *(FString(TEXT("/Script/Engine.")) + Asset->Kind));
        if (!TestTrue(TEXT("An actual typed original resource is available at its original identity: ") + Path,
                Live && Expected && Live->IsA(Expected) && Live->GetPathName() == Path &&
                    FString(Live->GetOutermost()->GetMetaData()->GetValue(Live, TEXT("P4BlueprintStandIn"))) !=
                        TEXT("1") &&
                    P4QueryOriginalAsset(Live).IsOriginal()))
            continue;
        ++TypedResources;
    }
    TestEqual(
        TEXT("Every planned resource is a real typed source asset; none is an empty stand-in or skipped reference"),
        TypedResources, Plan.Assets.Num());
    for (const FString& Package : Roots)
        TestTrue(
            TEXT("Resource preparation leaves the original particle for its separate strict native gate: ") + Package,
            !FindPackage(nullptr, *Package) && !FPackageName::DoesPackageExist(Package) &&
                !Imported.Imported.Contains(Package));
    return !HasAnyErrors();
}
#endif

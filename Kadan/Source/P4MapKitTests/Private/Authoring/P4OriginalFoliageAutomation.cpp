#include "P4OriginalAssetImport.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "P4RecoveredFoliageType.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "Engine/StaticMesh.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace
{
const TCHAR* const OriginalFoliageRelative = TEXT(
    "Carrot4/Terrain/Cave/Type/Soil/Foliages/Types/FT_CMA_T02_DC_Foliages08_PT0_FoliageType");
const TCHAR* const OriginalFoliageName = TEXT("FT_CMA_T02_DC_Foliages08_PT0_FoliageType");
FString OriginalFoliagePackage()
{
    return FString(TEXT("/Game/")) + OriginalFoliageRelative;
}
FString OriginalFoliagePath()
{
    return OriginalFoliagePackage() + TEXT(".") + OriginalFoliageName;
}
bool OriginalFoliageSourceDigest(const FString& Root, FString& Digest)
{
    TArray<uint8> Header, Body;
    const FString Base = Root / OriginalFoliageRelative;
    return FFileHelper::LoadFileToArray(Header, *(Base + TEXT(".uasset"))) &&
        FFileHelper::LoadFileToArray(Body, *(Base + TEXT(".uexp"))) &&
        P4HashSourceBuffers(Header.GetData(), Header.Num(), Body.GetData(), Body.Num(), Digest);
}
bool CheckOriginalFoliage(
    FAutomationTestBase& Test, UFoliageType_InstancedStaticMesh* Type, const FString& SourceDigest)
{
    if (!Test.TestNotNull(TEXT("Actual original foliage asset exists"), Type))
        return false;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Foliage.P4RecoveredFoliageType"));
    if (!Test.TestTrue(TEXT("Actual original uses the independently qualified native storage"),
            Class && Type->GetClass() == Class))
        return false;
    const auto* Name = FindFProperty<FNameProperty>(Class, TEXT("FoliageTypeName"));
    const auto* Toggle = FindFProperty<FBoolProperty>(Class, TEXT("bUseCustomRadius"));
    const auto* Radius = FindFProperty<FFloatProperty>(Class, TEXT("CustomRadius"));
    if (!Test.TestTrue(TEXT("Original game fields retain exact property types"),
            Name && Toggle && Toggle->IsNativeBool() && Radius))
        return false;
    Test.TestEqual(TEXT("Actual source's authored foliage category survives"), Name->GetPropertyValue_InContainer(Type),
        FName(TEXT("DeadLeaf")));
    Test.TestFalse(TEXT("Actual source's omitted custom radius toggle retains captured false default"),
        Toggle->GetPropertyValue_InContainer(Type));
    Test.TestEqual(TEXT("Actual source's omitted radius retains captured zero default"),
        Radius->GetPropertyValue_InContainer(Type), 0.f);
    const uint32 LowerBits[] = {0x7effffffu, 0x7effffffu, 0x7f800000u};
    Test.TestTrue(TEXT("Actual source's independent IEEE lower-bound words survive"),
        !FMemory::Memcmp(&Type->LowBoundOriginRadius, LowerBits, sizeof(LowerBits)));
    Test.TestTrue(TEXT("Actual source references the retained original mesh without replacement"),
        Type->Mesh &&
            Type->Mesh->GetPathName() ==
                TEXT(
                    "/Game/Carrot4/Terrain/Cave/Type/Soil/Foliages/Meshes/SM_CMA_T02_DC_Foliages08_PT0.SM_CMA_T02_DC_Foliages08_PT0"));
    const FP4OriginalStatus Status = P4QueryOriginalAsset(Type);
    // This query reads provenance only (Recorded/Dirty), without performing a
    // content comparison. The explicit diagnostic below proves unchanged state.
    Test.TestTrue(TEXT("Actual original provenance remains recorded"), Status.IsOriginal());
    Test.TestFalse(TEXT("No original foliage properties or native bytes are dropped"), Status.Record.ReferenceOnly);
    Test.TestEqual(TEXT("Actual saved original record retains exact source bytes digest"), Status.Record.SourceDigest,
        SourceDigest);
    const FP4OriginalDiagnostic Diagnostic = P4DiagnoseOriginalAsset(Type);
    Test.TestTrue(TEXT("Actual reloaded native asset diagnostic cook equals published original state without errors"),
        Diagnostic.Error.IsEmpty() && Diagnostic.Comparison == EP4OriginalComparison::Equal);
    if (!Diagnostic.Error.IsEmpty())
        Test.AddError(Diagnostic.Error);
    return !Test.HasAnyErrors();
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalFoliageImportTest, "P4MapKit.Original.FoliageImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalFoliageImportTest::RunTest(const FString&)
{
    const FString Source = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ARCHIVED_SOURCE"));
    if (Source.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "FoliageImport not exercised: set P4_NATIVE_ARCHIVED_SOURCE to retained ConvertedOriginals/Carrot4/Content; use retained project with its original leaf mesh already imported."));
        return true;
    }
    FString SourceDigest, Target;
    if (!TestTrue(TEXT("Actual retained source buffers are readable and independently hashed"),
            OriginalFoliageSourceDigest(Source, SourceDigest)))
        return false;
    if (!TestTrue(TEXT("Actual target resolves to this retained project"),
            FPackageName::TryConvertLongPackageNameToFilename(OriginalFoliagePackage(), Target, TEXT(".uasset"))))
        return false;
    if (!TestTrue(TEXT("Actual original target is free; no existing cache is replaced"),
            !FindPackage(nullptr, *OriginalFoliagePackage()) && !IFileManager::Get().FileExists(*Target)))
        return false;
    const auto Imported = P4ImportOriginalAssetNative(
        Source / OriginalFoliageRelative + TEXT(".uasset"), FString(), true, false);
    if (!TestTrue(TEXT("Actual original strict foliage import/save and full source cook comparison succeed"),
            Imported.IsValid()))
    {
        AddError(Imported.ErrorDetail);
        return false;
    }
    TestTrue(TEXT("New actual original is saved and fully compared"),
        Imported.Created && Imported.Saved && Imported.ComparedKnown && Imported.Compared > 0);
    TestFalse(TEXT("Normal original import requires no reference-only fallback"), Imported.ReferenceOnly);
    TestEqual(TEXT("No original foliage values are omitted"), Imported.ValuesNotHeldCount, 0);
    TestEqual(TEXT("No original foliage native bytes are omitted"), Imported.NativeBytesNotHeld, 0);
    CheckOriginalFoliage(*this, Cast<UFoliageType_InstancedStaticMesh>(Imported.Asset), SourceDigest);
    FString After;
    TestTrue(TEXT("Import/save leaves retained original source bytes unchanged"),
        OriginalFoliageSourceDigest(Source, After) && After == SourceDigest);
    // Keep this one tiny actual asset for the separate fresh-process reload.
    // Its original dependency/project/cache are already owned by the survey.
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalFoliageReloadTest, "P4MapKit.Original.FoliageReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalFoliageReloadTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_FOLIAGE_RELOAD")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "FoliageReload not exercised: run alone in a fresh editor with P4_NATIVE_ORIGINAL_FOLIAGE_RELOAD=1 after FoliageImport."));
        return true;
    }
    FString SourceDigest;
    if (!TestTrue(TEXT("Same retained source buffers are readable in fresh process"),
            OriginalFoliageSourceDigest(
                FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ARCHIVED_SOURCE")), SourceDigest)))
        return false;
    if (!TestNull(TEXT("Actual original package is not already loaded in the cold process"),
            FindPackage(nullptr, *OriginalFoliagePackage())))
        return false;
    auto* Type = LoadObject<UFoliageType_InstancedStaticMesh>(
        nullptr, *OriginalFoliagePath(), nullptr, LOAD_NoWarn | LOAD_Quiet);
    // This selector performs no factory/import call: ordinary UE loading must
    // retain the real source fields, source digest, IEEE words and original state.
    return CheckOriginalFoliage(*this, Type, SourceDigest);
}
#endif

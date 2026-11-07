#if P4_MAPKIT_TESTS
#include "P4OriginalLandscapePreview.h"
#include "P4KadanImport.h"
#include "P4OriginalRecord.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "LandscapeComponent.h"
#include "LandscapeMaterialInstanceConstant.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Engine/Texture2D.h"
#include "UObject/UnrealType.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "FileHelpers.h"
#include "StaticParameterSet.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
namespace P4SweepCapture
{
bool Capture(UWorld*, const FString&, const TSharedRef<FJsonObject>&);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalLandscapeOwnerGuardTest, "P4MapKit.Quality.OriginalLandscapeOwnerGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalLandscapeOwnerGuardTest::RunTest(const FString&)
{
    auto* Owner = NewObject<ULandscapeMaterialInstanceConstant>(GetTransientPackage());
    Owner->Parent = UMaterial::GetDefaultMaterial(MD_Surface);
    const FString Imported = P4OriginalLandscapeOwnerGuard(Owner);
    TestFalse(TEXT("an owner with a parent can be checked"), Imported.IsEmpty());
    // A fresh import compiles the owner, which lists the textures its shader used; the owner loaded from
    // disk has not compiled yet and lists none. The preview made at import must still match it.
    auto* Used = CastField<FArrayProperty>(UMaterialInstance::StaticClass()->FindPropertyByName(TEXT("CachedReferencedTextures")));
    if (!TestNotNull(TEXT("the engine keeps the compiled shader's textures"), Used))
        return false;
    FScriptArrayHelper Textures(Used, Used->ContainerPtrToValuePtr<void>(Owner));
    *reinterpret_cast<UObject**>(Textures.GetRawPtr(Textures.AddValue())) = UTexture2D::StaticClass()->GetDefaultObject();
    TestEqual(TEXT("the compile's texture list does not change the owner"), P4OriginalLandscapeOwnerGuard(Owner), Imported);
    FScalarParameterValue Added;
    Added.ParameterInfo = FMaterialParameterInfo(TEXT("AlignedTextureSize"));
    Added.ParameterValue = 601.f;
    Owner->ScalarParameterValues.Add(Added);
    TestNotEqual(TEXT("a creator's parameter does change it"), P4OriginalLandscapeOwnerGuard(Owner), Imported);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalLandscapePreviewTest, "P4MapKit.Editor.OriginalLandscapeColorPreviewCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalLandscapePreviewTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_LANDSCAPE_COLOR_CACHE")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned actual inline Landscape fixture."));
        return true;
    }
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME"));
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.AllowGameBackedLevels = true;
    const FString Private = FPaths::GetPath(Settings.GamePaks);
    Settings.Retoc = Private / TEXT("retoc/retoc.exe");
    Settings.Repak = Private / TEXT("repak/repak.exe");
    const FString Map = TEXT("/Game/Carrot4/Maps/Madori/Ddb/DDB_AI005/DDB_AI005_P");
    const FString LandscapePackage = TEXT("/Game/Carrot4/Maps/Madori/Ddb/DDB_AI005/Sublevels/DDB_AI005_Landscape");
    const FString Cache = FPaths::ProjectSavedDir() / TEXT("Kadan/LandscapeColorPreviews") /
        (FMD5::HashAnsiString(*LandscapePackage) + TEXT(".json"));
    FString PreviousCache;
    const bool HadCache = FFileHelper::LoadFileToString(PreviousCache, *Cache);
    if (HadCache &&
        !TestTrue(TEXT("The control excludes an earlier drawing cache"), IFileManager::Get().Delete(*Cache)))
        return false;
    ON_SCOPE_EXIT
    {
        if (HadCache && !IFileManager::Get().FileExists(*Cache))
            FFileHelper::SaveStringToFile(PreviousCache, *Cache, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    };
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The actual original DDB5 map opens"), World))
        return false;
    ON_SCOPE_EXIT
    {
        P4RestoreOriginalLandscapeColorPreviews();
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    P4RestoreOriginalLandscapeColorPreviews();
    auto Before = MakeShared<FJsonObject>();
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/OriginalLandscapeColorCache");
    if (!TestTrue(TEXT("The old original source material is captured"),
            P4SweepCapture::Capture(World, Folder / TEXT("before"), Before)))
        return false;
    const auto Imported = P4KadanRunImport(Settings, P4KadanPlanImport(Settings, TSet<FString>{Map}, true), true);
    FFileHelper::SaveStringToFile(
        Imported.Report, *(Folder / TEXT("import.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    for (const FString& Warning : Imported.Warnings)
        if (Warning.Contains(TEXT("landscape color preview")))
            AddInfo(Warning);
    TestTrue(TEXT("Normal original map import completes without replacing its assets"), Imported.Ok);
    ULandscapeMaterialInstanceConstant* Owner = nullptr;
    UWorld* SourceWorld = nullptr;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor))
                {
                    TInlineComponentArray<ULandscapeComponent*> Parts;
                    Actor->GetComponents(Parts);
                    for (auto* Part : Parts)
                        for (auto* Slot : Part->MaterialInstances)
                            for (auto* Instance = Slot; Instance;
                                Instance = Cast<UMaterialInstanceConstant>(Instance->Parent))
                                if (auto* Candidate = Cast<ULandscapeMaterialInstanceConstant>(Instance))
                                    if (Candidate->GetStaticParameters().TerrainLayerWeightParameters.Num())
                                    {
                                        Owner = Candidate;
                                        SourceWorld = Cast<UWorld>(Level->GetOuter());
                                    }
                }
    if (!TestTrue(TEXT("The actual source shader owner is retained"), Owner && SourceWorld))
        return false;
    UMaterialInterface* Parent = Owner->Parent;
    P4ApplyOriginalLandscapeColorPreviews(World);
    TestTrue(
        TEXT("The normal import creates a usable source drawing cache"), P4OriginalLandscapeColorPreviewCount() > 0);
    TestTrue(TEXT("The source's original material parent remains untouched"), Owner->Parent == Parent);
    auto After = MakeShared<FJsonObject>();
    TestTrue(
        TEXT("The source-derived floor is captured"), P4SweepCapture::Capture(World, Folder / TEXT("after"), After));
    TestTrue(TEXT("The formerly black floor becomes visible"),
        After->GetArrayField(TEXT("captures"))[0]->AsObject()->GetNumberField(TEXT("lit_pixels")) >
            Before->GetArrayField(TEXT("captures"))[0]->AsObject()->GetNumberField(TEXT("lit_pixels")) +
                1280 * 720 / 5);
    const auto Compared = P4DiagnoseOriginalAsset(SourceWorld);
    TestTrue(TEXT("Drawing with the cached source color preserves exact original level state"),
        Compared.Error.IsEmpty() && Compared.Comparison == EP4OriginalComparison::Equal);
    P4RestoreOriginalLandscapeColorPreviews();
    const auto Values = Owner->ScalarParameterValues;
    FScalarParameterValue Added;
    Added.ParameterInfo = FMaterialParameterInfo(TEXT("AlignedTextureSize"));
    Added.ParameterValue = 601.f;
    Owner->ScalarParameterValues.Add(Added);
    TestEqual(TEXT("An actual creator parameter override invalidates the original drawing cache"),
        P4ApplyOriginalLandscapeColorPreviews(World), 0);
    TestTrue(TEXT("The creator parameter remains exactly as entered"),
        Owner->ScalarParameterValues.Last().ParameterValue == 601.f);
    Owner->ScalarParameterValues = Values;
    UEditorLoadingAndSavingUtils::NewBlankMap(false);
    World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    TestTrue(TEXT("Ordinary cold map opening activates the source drawing cache"),
        World && P4OriginalLandscapeColorPreviewCount() > 0);
    return !HasAnyErrors();
}
#endif

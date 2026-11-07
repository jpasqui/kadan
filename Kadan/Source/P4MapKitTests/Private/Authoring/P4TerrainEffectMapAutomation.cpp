#if P4_MAPKIT_TESTS
#include "P4KadanImport.h"
#include "P4OriginalRecord.h"
#include "P4OriginalDecode.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "UObject/UnrealType.h"
#include "HAL/FileManager.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TerrainEffectMapTest, "P4MapKit.Original.TerrainEffectMapKeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TerrainEffectMapTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_TERRAIN_EFFECT_MAP")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned original terrain-effect fixture."));
        return true;
    }
    const FString Package = TEXT("/Game/Carrot4/Terrain/Params/TerrainEffectParamCmn");
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME"));
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    const FString Private = FPaths::GetPath(Settings.GamePaks);
    Settings.Retoc = Private / TEXT("retoc/retoc.exe");
    Settings.Repak = Private / TEXT("repak/repak.exe");
    const auto Plan = P4KadanPlanImport(Settings, TSet<FString>{Package}, false);
    const auto Imported = P4KadanRunImport(Settings, Plan, false);
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit");
    IFileManager::Get().MakeDirectory(*Folder, true);
    FFileHelper::SaveStringToFile(Imported.Report, *(Folder / TEXT("TerrainEffectMapImport.json")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (!TestTrue(TEXT("The actual terrain-effect asset imports and saves through the normal pipeline"),
            Imported.Imported.Contains(Package)))
        return false;
    UObject* Asset = LoadObject<UObject>(nullptr, *(Package + TEXT(".TerrainEffectParamCmn")));
    if (!TestNotNull(TEXT("The imported original terrain-effect asset is available"), Asset))
        return false;
    std::vector<P4MapKit::FMapStructCodec> Codecs;
    TestTrue(TEXT("The owning schema provides its actual map serialization traits"),
        P4MapStructCodecs(Asset->GetClass(), Codecs));
    TestTrue(TEXT("GameplayTag map keys are tagged archive structs, regardless of their net serializer"),
        std::any_of(Codecs.begin(), Codecs.end(), [](const P4MapKit::FMapStructCodec& C)
            { return C.Property == "Params" && C.Key == "GameplayTag" && !C.KeyNative; }));
    for (const TCHAR* Name : {TEXT("Params"), TEXT("ConvertData")})
    {
        const auto* Field = FindFProperty<FMapProperty>(Asset->GetClass(), Name);
        if (!TestNotNull(TEXT("The actual original map is retained: ") + FString(Name), Field))
            return false;
        FScriptMapHelper Values(Field, Field->ContainerPtrToValuePtr<void>(Asset));
        TestEqual(TEXT("All 27 original terrain-effect entries survive: ") + FString(Name), Values.Num(), 27);
    }
    const auto Check = P4DiagnoseOriginalAsset(Asset);
    TestTrue(TEXT("The saved original asset matches its complete native source baseline"),
        Check.Error.IsEmpty() && Check.Comparison == EP4OriginalComparison::Equal);
    return !HasAnyErrors();
}
#endif

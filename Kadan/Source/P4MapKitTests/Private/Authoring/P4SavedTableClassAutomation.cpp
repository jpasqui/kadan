#if P4_MAPKIT_TESTS
#include "P4KadanImport.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ColdSavedTableClassTest, "P4MapKit.Kadan.ColdSavedClassTableDependencies",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ColdSavedTableClassTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SAVED_TABLE_CLASS")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned cold DDB table fixture."));
        return true;
    }
    const FString ClassPackage = TEXT("/Game/Carrot4/Placeables/Objects/VS/GVSSPBomb");
    const FString ClassPath = ClassPackage + TEXT(".GVSSPBomb_C");
    if (!TestTrue(TEXT("The actual dependency is saved but starts unloaded in this fresh editor"),
                  FPackageName::DoesPackageExist(ClassPackage) && !FindObject<UClass>(nullptr, *ClassPath)))
        return false;
    FString ClassFile, BeforeHash;
    int64 BeforeBytes = 0;
    if (!TestTrue(TEXT("The preserved dependency's actual saved bytes are readable"),
                  FPackageName::DoesPackageExist(ClassPackage, nullptr, &ClassFile) &&
                      P4HashFile(ClassFile, 64 * 1024 * 1024, BeforeBytes, BeforeHash)))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME"));
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    const FString Private = FPaths::GetPath(Settings.GamePaks);
    Settings.Retoc = Private / TEXT("retoc/retoc.exe");
    Settings.Repak = Private / TEXT("repak/repak.exe");
    Settings.AllowGameBackedLevels = true;
    const TSet<FString> Roots = {TEXT("/Game/Carrot4/Core/VS/Pop/SettingTable/SPBomb/DT_Pop_SPBomb_05"),
                                 TEXT("/Game/Carrot4/Core/VS/Pop/SettingTable/SPBomb/DT_Pop_SPBomb_06"),
                                 TEXT("/Game/Carrot4/Core/VS/Pop/VPP_06")};
    const auto Plan = P4KadanPlanImport(Settings, Roots, true);
    if (!TestTrue(TEXT("The real table-only request has a valid source plan"), Plan.Problems.Num() == 0))
        return false;
    const auto Imported = P4KadanRunImport(Settings, Plan, true);
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit");
    IFileManager::Get().MakeDirectory(*Folder, true);
    FFileHelper::SaveStringToFile(Imported.Report, *(Folder / TEXT("ColdSavedClassTables.json")),
                                  FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TestTrue(TEXT("Cold preserved classes do not prevent native table-only imports"),
             Imported.Ok && !Imported.Cancelled);
    TSharedPtr<FJsonObject> Receipt;
    TestTrue(TEXT("Deferred tables finish even when no new Blueprint batch runs"),
             FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Imported.Report), Receipt) &&
                 Receipt.IsValid() && !Receipt->HasField(TEXT("blueprints")));
    TestTrue(TEXT("The genuine saved generated class becomes available"),
             IsValid(FindObject<UClass>(nullptr, *ClassPath)));
    FString AfterHash;
    int64 AfterBytes = 0;
    TestTrue(TEXT("Resolving the preserved class leaves its saved package byte-for-byte unchanged"),
             P4HashFile(ClassFile, 64 * 1024 * 1024, AfterBytes, AfterHash) && BeforeBytes == AfterBytes &&
                 P4ExactNativeString(BeforeHash, AfterHash));
    for (const FString& Root : Roots)
    {
        UObject* Asset = LoadObject<UObject>(nullptr, *(Root + TEXT(".") + FPackageName::GetShortName(Root)));
        TestTrue(TEXT("The actual deferred asset is reconstructed rather than retained as a game reference: ") + Root,
                 Asset && !Imported.ReferenceOnly.Contains(Root) && !Imported.NotImported.Contains(Root));
        const auto Compared = P4DiagnoseOriginalAsset(Asset);
        TestTrue(TEXT("The completed table/parameter asset matches its original native state: ") + Root,
                 Compared.Error.IsEmpty() && Compared.Comparison == EP4OriginalComparison::Equal);
    }
    return !HasAnyErrors();
}
#endif

#if P4_MAPKIT_TESTS
#include "P4KadanImport.h"
#include "P4OriginalClassComparison.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalClassRefresh, "P4MapKit.Kadan.RefreshActualArea500Classes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalClassRefresh::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_AREA500_CLASS_REFRESH")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned Area500 original class fixture."));
        return true;
    }
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME"));
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.AllowGameBackedLevels = true;
    const FString Private = FPaths::GetPath(Settings.GamePaks);
    Settings.Retoc = Private / TEXT("retoc/retoc.exe");
    Settings.Repak = Private / TEXT("repak/repak.exe");
    TSet<FString> Roots = {TEXT("/Game/Carrot4/Placeables/Objects/OjamaBlock/NavType/Area500/GMultiBlockArea500_01A"),
        TEXT("/Game/Carrot4/Placeables/Buddy/GHappyBase"),
        TEXT("/Game/Carrot4/Placeables/Gimmicks/Excavation/GExcavationBase"),
        TEXT("/Game/Carrot4/Placeables/Gimmicks/Excavation/GExcavationM"),
        TEXT("/Game/Carrot4/Placeables/Gimmicks/Excavation/GExcavationS"),
        TEXT("/Game/Carrot4/Placeables/Gimmicks/Excavation/GExcavationOnyon"),
        TEXT("/Game/Carrot4/Placeables/Objects/Madori/GMadoriPoko"),
        TEXT("/Game/Carrot4/Placeables/Objects/Madori/GMadoriRuins")};
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_AREA500_CLASS_REFRESH_REMAINING")) == TEXT("1"))
        for (auto It = Roots.CreateIterator(); It; ++It)
            if (!It->EndsWith(TEXT("/GHappyBase")) && !It->EndsWith(TEXT("/GMadoriPoko")) &&
                !It->EndsWith(TEXT("/GMadoriRuins")))
                It.RemoveCurrent();
    const FString Selection = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CLASS_REFRESH_SELECTION"));
    if (!Selection.IsEmpty())
    {
        FString Text;
        TArray<TSharedPtr<FJsonValue>> Packages;
        if (!TestTrue(TEXT("The explicit diagnostic class selection is readable"),
                FFileHelper::LoadFileToString(Text, *Selection) &&
                    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Packages) && Packages.Num() > 0 &&
                    Packages.Num() <= 64))
            return false;
        Roots.Reset();
        for (const auto& Package : Packages)
            Roots.Add(Package->AsString());
    }
    auto Plan = P4KadanPlanImport(Settings, Roots, false);
    if (!TestTrue(TEXT("The selected real original classes have a readable source plan"), Plan.Problems.Num() == 0))
        return false;
    Plan.Assets.RemoveAll(
        [&](const TSharedPtr<FP4KadanSourceAsset>& Asset) { return !Roots.Contains(Asset->Package); });
    if (!TestEqual(
            TEXT("Every selected original class is included in the focused repair"), Plan.Assets.Num(), Roots.Num()))
        return false;
    auto Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Before;
    for (const auto& Asset : Plan.Assets)
    {
        TSharedPtr<FJsonObject> Compared;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P4OriginalClassComparisonJson(
                                         P4CompareOriginalClassNative(Asset->Header, Asset->Body))),
            Compared);
        if (Compared.IsValid())
        {
            Compared->SetStringField(TEXT("requested_package"), Asset->Package);
            Before.Add(MakeShared<FJsonValueObject>(Compared));
        }
    }
    Report->SetArrayField(TEXT("before"), Before);
    const auto Imported = P4KadanRunImport(Settings, Plan, false);
    TSharedPtr<FJsonObject> Receipt;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Imported.Report), Receipt);
    if (Receipt.IsValid())
        Report->SetObjectField(TEXT("import"), Receipt);
    TArray<TSharedPtr<FJsonValue>> After;
    for (const auto& Asset : Plan.Assets)
    {
        const auto Result = P4CompareOriginalClassNative(Asset->Header, Asset->Body);
        TestTrue(TEXT("The actual class is reconstructed by its ordinary guarded importer: ") + Asset->Package,
            Imported.Imported.Contains(Asset->Package));
        TestTrue(TEXT("The refreshed original class matches its actual source: ") + Asset->Package,
            Result.IsValid() && Result.DefaultsReadable && Result.Same);
        if (!Result.Same)
            AddInfo(
                Asset->Package + TEXT(" ") + (Result.OnlyGameSays.Num() ? Result.OnlyGameSays[0] : Result.ErrorDetail));
        TSharedPtr<FJsonObject> Compared;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P4OriginalClassComparisonJson(Result)), Compared);
        if (Compared.IsValid())
        {
            Compared->SetStringField(TEXT("requested_package"), Asset->Package);
            After.Add(MakeShared<FJsonValueObject>(Compared));
        }
    }
    Report->SetArrayField(TEXT("after"), After);
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit");
    IFileManager::Get().MakeDirectory(*Folder, true);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    const FString ReceiptName = Selection.IsEmpty() ? TEXT("Area500-class-refresh.json")
                                                    : TEXT("selected-class-refresh.json");
    TestTrue(TEXT("The actual before/import/after class evidence is retained"),
        FFileHelper::SaveStringToFile(
            Json, *(Folder / ReceiptName), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return !HasAnyErrors();
}
#endif

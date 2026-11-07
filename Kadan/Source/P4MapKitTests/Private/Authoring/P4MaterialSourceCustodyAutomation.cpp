#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialRecovery.h"
#include "P4MaterialGraph.h"
#include "P4MaterialInstance.h"
#include "Dom/JsonObject.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialSourceCustodyGuardsTest, "P4MapKit.Kadan.MaterialSourceCustodyGuards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialSourceCustodyGuardsTest::RunTest(const FString&)
{
    const FString Prefix = TEXT("/Game/P4MapKitTests/SourceCustody_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const auto Refused = [&](const FString& Text, const FString& Package)
    {
        TSharedPtr<FJsonObject> Report;
        bool Valid = true, Saved = true;
        FString Error;
        return TestTrue(TEXT("Real builder returns a structured refusal"),
                   FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Report) && Report.IsValid() &&
                       Report->TryGetBoolField(TEXT("valid"), Valid) && !Valid &&
                       Report->TryGetBoolField(TEXT("saved"), Saved) && !Saved &&
                       Report->TryGetStringField(TEXT("error"), Error) &&
                       Error.StartsWith(TEXT("material-source-current-refused: control-source-changed"),
                           ESearchCase::CaseSensitive)) &&
            TestFalse(TEXT("Refused source publishes no file"), FPackageName::DoesPackageExist(Package)) &&
            TestNull(TEXT("Refused source publishes no live asset"),
                FindObject<UObject>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package))));
    };
    int32 Calls = 0;
    const auto Changed = [&](FString& Error)
    {
        ++Calls;
        Error = TEXT("control-source-changed");
        return false;
    };
    const TArray<TSharedPtr<FP4KadanSourceAsset>> Empty;
    FP4MaterialRecovery Owner(FString(), Empty);
    FP4MaterialRecovery Recipe(Owner, Empty);
    const auto Result = Recipe.Import(Prefix + TEXT("_Recovery"), true, Changed);
    TestFalse(TEXT("A changed source cannot enter recovery"), Result.bOk);
    TestTrue(TEXT("No recipe/source was read"), Result.Source.IsEmpty());
    Refused(Result.Report, Prefix + TEXT("_Recovery"));
    TestEqual(TEXT("Recovery checks custody before reading missing sources/archive"), Calls, 1);
    TestEqual(TEXT("A rejected source never reads the shared shader archive"), Owner.ShaderArchiveReadAttempts(), 0);
    TestEqual(TEXT("A rejected source never starts a family shader recovery"), Recipe.FamilyRecoveryBuilds(), 0);

    Calls = 0;
    Refused(P4BuildMaterialGraphReport(TEXT("intentionally invalid JSON"), Prefix + TEXT("_Master"), true, Changed),
        Prefix + TEXT("_Master"));
    TestEqual(TEXT("Master custody refuses before graph construction/parsing"), Calls, 1);
    Calls = 0;
    Refused(
        P4BuildMaterialInstanceReport(TEXT("intentionally invalid JSON"), Prefix + TEXT("_Instance"), true, Changed),
        Prefix + TEXT("_Instance"));
    TestEqual(TEXT("MIC custody refuses before instance construction"), Calls, 1);

    // Exercise a real created MIC followed by source drift before publication.
    // Its builtin parent is read only; no game source/tools/archive is needed.
    UMaterial* Parent = UMaterial::GetDefaultMaterial(MD_Surface);
    if (!TestNotNull(TEXT("A real builtin parent is available"), Parent))
        return false;
    const bool ParentDirty = Parent->GetOutermost()->IsDirty();
    const TSharedRef<FJsonObject> Source = MakeShared<FJsonObject>();
    Source->SetStringField(TEXT("parent"), Parent->GetPathName());
    FString Instance;
    FJsonSerializer::Serialize(Source, TJsonWriterFactory<>::Create(&Instance));
    Calls = 0;
    const auto ChangedAfterRead = [&](FString& Error)
    {
        if (++Calls == 1)
            return true;
        Error = TEXT("control-source-changed");
        return false;
    };
    Refused(P4BuildMaterialInstanceReport(Instance, Prefix + TEXT("_Late"), true, ChangedAfterRead),
        Prefix + TEXT("_Late"));
    TestEqual(TEXT("Source is rechecked after native MIC readback before save"), Calls, 2);
    const FString SavedPackage = Prefix + TEXT("_SavedDrift");
    bool SawNativeSave = false;
    const FDelegateHandle Saved = FCoreUObjectDelegates::OnObjectSaved.AddLambda(
        [&](UObject* Object)
        {
            if (Object && Object->GetOutermost()->GetName() == SavedPackage)
                SawNativeSave = true;
        });
    const auto ChangedDuringSave = [&](FString& Error)
    {
        if (!SawNativeSave)
            return true;
        Error = TEXT("control-source-changed-during-save");
        return false;
    };
    const FString SavedReport = P4BuildMaterialInstanceReport(Instance, SavedPackage, true, ChangedDuringSave);
    FCoreUObjectDelegates::OnObjectSaved.Remove(Saved);
    TestTrue(TEXT("The late-drift control reached actual native serialization"), SawNativeSave);
    Refused(SavedReport, SavedPackage);
    TestEqual(
        TEXT("Parent native state remains clean/dirty as before"), Parent->GetOutermost()->IsDirty(), ParentDirty);
    return !HasAnyErrors();
}
#endif

#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "P4BlueprintImport.h"
#include "P4KadanImport.h"
#include "P4KadanImportSources.h"
#include "P4KadanBlueprintPlans.h"
#include "P4DecodedPropertyWriter.h"
#include "P4LevelImportWorld.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "P4OriginalClassComparison.h"
#include "P4MapKitCore/property_decoder.h"
#include "Components/DirectionalLightComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace P4ActualEnvLightTests
{
const FString Package = TEXT("/Game/Carrot4/Core/Graphics/System/BP_EnvController");
const FString Object = Package + TEXT(".BP_EnvController");
const FString Storage = TEXT("/Script/P4StubsCore.P4RecoveredDirectionalLightComponent");
using V = P4MapKit::FDecodedValue;
using FValues = TMap<FName, V>;

bool ReadJson(const FString& Path, TSharedPtr<FJsonObject>& Out)
{
    FString Text;
    return FFileHelper::LoadFileToString(Text, *Path) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Out) && Out.IsValid();
}
bool HashMatches(const FString& Path, const FString& Expected)
{
    FString Hash;
    int64 Size = 0;
    return P4HashFile(Path, 1024ll * 1024 * 1024, Size, Hash) && P4ExactNativeString(Hash, Expected);
}
FString Normalize(FString Path)
{
    FPaths::NormalizeFilename(Path);
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::CollapseRelativeDirectories(Path);
    FPaths::NormalizeDirectoryName(Path);
    return Path;
}
bool ForkName(const FString& Name)
{
    return Name.StartsWith(TEXT("C4"), ESearchCase::CaseSensitive) || Name == TEXT("bC4Param") ||
        Name == TEXT("bC4ShadowBoundsOverride");
}
bool Captured(const TSharedPtr<FJsonValue>& Json, V& Out)
{
    if (!Json.IsValid())
        return false;
    switch (Json->Type)
    {
        case EJson::Null:
            Out.Kind = V::EKind::Null;
            return true;
        case EJson::Boolean:
            Out = V::Atom(V::EKind::Boolean, Json->AsBool() ? "true" : "false");
            return true;
        case EJson::Number:
            Out = V::Atom(V::EKind::Number, TCHAR_TO_UTF8(*FString::Printf(TEXT("%.17g"), Json->AsNumber())));
            return true;
        case EJson::Object:
        {
            const auto Struct = Json->AsObject();
            const TSharedPtr<FJsonObject>* Fields = nullptr;
            if (!Struct->TryGetObjectField(TEXT("fields"), Fields))
                return false;
            Out.Kind = V::EKind::Object;
            for (const auto& Pair : (*Fields)->Values)
            {
                V Value;
                if (!Captured(Pair.Value, Value))
                    return false;
                Out.Fields.emplace_back(TCHAR_TO_UTF8(*Pair.Key), MoveTemp(Value));
            }
            return true;
        }
        default:
            return false;
    }
}
struct FFixture
{
    TSharedPtr<FJsonObject> Manifest;
    FP4KadanSourceBinding Source;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    FValues Defaults, Sun;
    FString Folder, AssetFile;

    bool Inputs(FAutomationTestBase& Test) const
    {
        const auto Ledger = Manifest->GetObjectField(TEXT("input_ledger"));
        for (const auto& Pair : Ledger->Values)
        {
            const FString Relative = Pair.Key;
            const auto Row = Pair.Value->AsObject();
            const FString Target = Normalize(FPaths::ProjectDir() / Relative);
            if (!Test.TestTrue(TEXT("Every reused input stays within the isolated project"),
                    FPaths::IsUnderDirectory(Target, Normalize(FPaths::ProjectDir())) &&
                        !Relative.EndsWith(TEXT(".umap")) && !Relative.Contains(TEXT("BP_EnvController"))))
                return false;
            const FString Expected = Row->GetStringField(TEXT("sha256"));
            if (!Test.TestTrue(*FString::Printf(TEXT("Reused dependency/SDK input stays byte exact: %s"), *Relative),
                    HashMatches(Target, Expected) && HashMatches(Row->GetStringField(TEXT("source")), Expected)))
                return false;
        }
        FString Error;
        return Test.TestTrue(
            TEXT("Actual original source H+B stays unchanged"), P4KadanSourceBindingCurrent(Source, Error));
    }
    bool Open(FAutomationTestBase& Test)
    {
        const FString File = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ENV_LIGHT_FIXTURE"));
        if (File.IsEmpty())
        {
            Test.AddError(TEXT("Set P4_NATIVE_ENV_LIGHT_FIXTURE to the prepared isolated fixture.json"));
            return false;
        }
        if (!Test.TestTrue(TEXT("Read isolated fixture custody"), ReadJson(File, Manifest)))
            return false;
        const FString Root = Normalize(FPaths::GetPath(File)), Project = Normalize(FPaths::ProjectDir());
        if (!Test.TestTrue(TEXT("Only the bounded owned native-light-env project is admitted"),
                Manifest->GetStringField(TEXT("owner")) == TEXT("native-env-light-proof/v1") &&
                    Manifest->GetStringField(TEXT("status")) == TEXT("prepared") &&
                    FPaths::GetCleanFilename(Root) == TEXT("native-light-env") &&
                    FPaths::GetCleanFilename(FPaths::GetPath(Root)) == TEXT("private") &&
                    FPaths::IsSamePath(Project, Root / TEXT("project")) &&
                    FPaths::IsSamePath(Manifest->GetStringField(TEXT("project")), FPaths::GetProjectFilePath()) &&
                    Manifest->GetStringField(TEXT("package")) == Package &&
                    Manifest->GetStringField(TEXT("sdk_build_id")) == TEXT("14830424") &&
                    !Manifest->GetBoolField(TEXT("old_env_package_linked")) &&
                    Manifest->GetIntegerField(TEXT("maps_linked")) == 0))
            return false;
        for (const auto& Item : Manifest->GetArrayField(TEXT("sources")))
        {
            const auto Row = Item->AsObject();
            auto Asset = MakeShared<FP4KadanSourceAsset>();
            Asset->Package = Row->GetStringField(TEXT("package"));
            Asset->Header = Row->GetStringField(TEXT("header"));
            Asset->Body = Row->GetStringField(TEXT("body"));
            // Only Env is selected for Blueprint planning/declaration; dependency
            // source pairs supply original object identities, never new assets.
            Asset->Kind = Asset->Package == Package ? TEXT("Blueprint") : TEXT("Reference");
            Sources.Add(Asset);
            if (!Test.TestTrue(TEXT("Selected original dependency pair remains source bound"),
                    HashMatches(Asset->Header, Row->GetStringField(TEXT("header_sha256"))) &&
                        HashMatches(Asset->Body, Row->GetStringField(TEXT("body_sha256")))))
                return false;
        }
        FP4KadanImportSourceScope Scope(Sources);
        FString Error;
        if (!Test.TestTrue(TEXT("Read actual canonical Env source without source-value handsets"),
                P4KadanReadBoundSource(Package, Source, Error)))
        {
            Test.AddError(Error);
            return false;
        }
        if (!Test.TestEqual(TEXT("Exact Env H+B custody"), Source.SourceDigest,
                Manifest->GetObjectField(TEXT("source"))->GetStringField(TEXT("source_digest"))))
            return false;
        TSharedPtr<FJsonObject> Capture;
        if (!Test.TestTrue(TEXT("Original native defaults match SDK producer input"),
                HashMatches(Manifest->GetStringField(TEXT("defaults_file")),
                    Manifest->GetStringField(TEXT("defaults_sha256"))) &&
                    ReadJson(Manifest->GetStringField(TEXT("defaults_file")), Capture)))
            return false;
        const auto Own = Capture->GetObjectField(TEXT("classes"))
                             ->GetObjectField(TEXT("/Script/Engine.DirectionalLightComponent"))
                             ->GetObjectField(TEXT("own"));
        for (const auto& Pair : Own->Values)
            if (ForkName(Pair.Key))
            {
                V Value;
                if (!Test.TestTrue(
                        TEXT("Captured native default has an exact typed representation"), Captured(Pair.Value, Value)))
                    return false;
                Defaults.Add(FName(*Pair.Key), MoveTemp(Value));
            }
        if (!Test.TestEqual(TEXT("All 25 captured source native light defaults are checked"), Defaults.Num(), 25))
            return false;
        const std::vector<uint8> H(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num()),
            B(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num());
        const auto Inventory = P4MapKit::ReadPackage(H, B);
        if (!Test.TestTrue(TEXT("Actual Env package parses with canonical source identity"),
                Inventory.Error.Code.empty() && FString(UTF8_TO_TCHAR(Inventory.Package.c_str())) == Package))
            return false;
        int32 Lights = 0;
        for (const auto& Export : Inventory.Exports)
            if (Export.Name == "SunLight" || Export.Name == "StaticSunLight")
            {
                ++Lights;
                std::string Class;
                if (!Test.TestTrue(TEXT("Original subobject class is exactly Engine.DirectionalLightComponent"),
                        Inventory.Reference(Export.Class, Class) &&
                            Class == "/Script/Engine.DirectionalLightComponent"))
                    return false;
                P4MapKit::FPropertyDecoder Decoder(Inventory, B);
                V Values;
                auto At = Export.Offset;
                if (!Test.TestTrue(TEXT("Decode actual complete light property tags"),
                        Decoder.Properties(At, Export.Offset + Export.Size, Values)))
                    return false;
                int32 Count = 0;
                for (const auto& Pair : Values.Fields)
                    if (ForkName(UTF8_TO_TCHAR(Pair.first.c_str())))
                    {
                        ++Count;
                        if (Export.Name == "SunLight")
                            Sun.Add(FName(UTF8_TO_TCHAR(Pair.first.c_str())), Pair.second);
                    }
                if (!Test.TestEqual(TEXT("Source light overrides are explicit; no CDO borrowing"), Count,
                        Export.Name == "SunLight" ? 17 : 0))
                    return false;
            }
        if (!Test.TestEqual(TEXT("Both actual original native light subobjects covered"), Lights, 2))
            return false;
        Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/ActualEnvLight");
        IFileManager::Get().MakeDirectory(*Folder, true);
        AssetFile = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
        return Inputs(Test);
    }
};

bool CheckActor(FAutomationTestBase& Test, UObject* Actor, const FFixture& Fixture, const TCHAR* Stage)
{
    if (!Test.TestNotNull(TEXT("Actual source Blueprint native actor/CDO exists"), Actor))
        return false;
    for (const TCHAR* Name : {TEXT("SunLight"), TEXT("StaticSunLight")})
    {
        const auto* Slot = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), Name);
        UObject* Light = Slot ? Slot->GetObjectPropertyValue_InContainer(Actor) : nullptr;
        if (!Test.TestTrue(TEXT("Actual original named subobject uses exact UHT child storage"),
                Light && Light->GetOuter() == Actor && Light->GetFName() == FName(Name) &&
                    Light->GetClass()->GetPathName() == Storage &&
                    Light->GetClass()->GetSuperClass() == UDirectionalLightComponent::StaticClass()))
            return false;
        int32 Count = 0;
        for (const auto& Pair : Fixture.Defaults)
        {
            const V* Value = Fixture.Sun.Find(Pair.Key);
            if (FString(Name) != TEXT("SunLight") || !Value)
                Value = &Pair.Value;
            const FProperty* Field = Light->GetClass()->FindPropertyByName(Pair.Key);
            if (!Test.TestTrue(TEXT("Each captured field is owned by genuine typed storage"),
                    Field && Field->GetOwnerStruct() == Light->GetClass()))
                return false;
            void* Scratch = FMemory::Malloc(Field->GetSize(), Field->GetMinAlignment());
            Field->InitializeValue(Scratch);
            ON_SCOPE_EXIT
            {
                Field->DestroyValue(Scratch);
                FMemory::Free(Scratch);
            };
            FP4DecodedPropertyWriter Writer({}, true);
            const bool Exact = Writer.Value(Field, Scratch, *Value, Pair.Key.ToString()) &&
                Field->Identical(Field->ContainerPtrToValuePtr<void>(Light), Scratch, PPF_None);
            if (!Test.TestTrue(*FString::Printf(TEXT("%s %s.%s equals actual source/default bits"), Stage, Name,
                                   *Pair.Key.ToString()),
                    Exact))
            {
                Test.AddError(Writer.Error);
                return false;
            }
            ++Count;
        }
        if (!Test.TestEqual(TEXT("Full observed native field set survives construction"), Count, 25))
            return false;
    }
    return true;
}

bool Construct(FAutomationTestBase& Test, UBlueprint* BP, const FFixture& Fixture)
{
    if (!Test.TestTrue(TEXT("Actual recovered Blueprint has its genuine GeneratedClass/CDO"),
            BP && BP->GeneratedClass && BP->GeneratedClass->GetDefaultObject() &&
                !P4BlueprintReconstructionIncomplete(BP) && !P4IsVerifiedStandIn(BP)))
        return false;
    FP4OriginalRecord Record;
    FString Error;
    if (!Test.TestTrue(TEXT("Recovered Blueprint policy marker is preserved, including ReferenceOnly"),
            P4ReadOriginalRecord(BP, Record, Error) && Record.Package == Package && Record.Kind == TEXT("blueprint") &&
                Record.Fidelity == TEXT("recovered") && Record.SourceDigestKind == TEXT("blueprint-import") &&
                Record.ReferenceOnly))
        return false;
    if (!CheckActor(Test, BP->GeneratedClass->GetDefaultObject(), Fixture, TEXT("actual-source-CDO")))
        return false;
    TStrongObjectPtr<UWorld> World(P4CreateOriginalImportWorld(
        MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("ActualEnvLight")),
        GetTransientPackage()));
    if (!Test.TestNotNull(TEXT("Independent transient construction world exists"), World.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        World->MarkPendingKill();
    };
    FActorSpawnParameters Params;
    Params.bDeferConstruction = true;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* Actor = World->SpawnActor<AActor>(BP->GeneratedClass, FTransform::Identity, Params);
    if (!Test.TestNotNull(TEXT("Actual source GeneratedClass spawns through ordinary native constructor"), Actor))
        return false;
    Actor->FinishSpawning(FTransform::Identity);
    if (!CheckActor(Test, Actor, Fixture, TEXT("FinishSpawning")))
        return false;
    for (int32 Round = 0; Round < 3; ++Round)
    {
        Actor->RerunConstructionScripts();
        CollectGarbage(RF_NoFlags);
        if (!CheckActor(Test, Actor, Fixture, *FString::Printf(TEXT("construction+GC-%d"), Round + 1)))
            return false;
    }
    Test.AddInfo(TEXT(
        "Only native light storage/defaults/source construction qualified here; nine post-process source values and game custom rendering semantics remain separate pending support."));
    return Fixture.Inputs(Test);
}
bool SaveReceipt(const FString& File, const TSharedRef<FJsonObject>& Json)
{
    FString Text;
    auto Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
    return FJsonSerializer::Serialize(Json, Writer) &&
        FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
} //namespace P4ActualEnvLightTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualEnvLightSourceImport,
    "P4MapKit.EngineDeclarations.ActualEnvLightSourceImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualEnvLightSourceImport::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ENV_LIGHT_FIXTURE")).IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Opt-in actual source proof: prepare the bounded native-light-env fixture first."));
        return true;
    }
    using namespace P4ActualEnvLightTests;
    FFixture Fixture;
    if (!Fixture.Open(*this))
        return false;
    if (!TestNull(TEXT("Fresh Env was never loaded"), FindPackage(nullptr, *Package)) ||
        !TestFalse(TEXT("Fresh Env has no prior saved package"), IFileManager::Get().FileExists(*Fixture.AssetFile)))
        return false;
    FP4KadanImportSourceScope Scope(Fixture.Sources);
    FP4KadanBlueprintPlans Plans;
    FString Error;
    FP4KadanImportSettings Settings;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    FP4CurrentGameRegistryPtr Reader;
    if (!TestTrue(TEXT("Ordinary native planner uses only the actual source Env Blueprint"),
            P4KadanPlanBlueprints(
                Fixture.Sources, {Package}, Fixture.Folder / TEXT("plans"), Plans, Error, Settings, Reader)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(TEXT("No other Blueprint is declared or imported"),
            Plans.Planned.Num() == 1 && Plans.Planned[0] == Package && Plans.Refused.Num() == 0))
        return false;
    const auto Result = UP4BlueprintImport::RunSelectedNative(
        Plans.Folder, Fixture.Folder / TEXT("import-result.json"), {Package}, false, {}, true);
    if (!TestTrue(TEXT("Actual source native Declare/Values/Compile/Save completes without stand-ins"),
            Result.bOk && Result.Count == 1 && Result.CompletedPackages.Num() == 1 &&
                Result.CompletedPackages.Contains(FName(*Package))))
    {
        for (const auto& Why : Result.Errors)
            AddError(Why);
        return false;
    }
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Object);
    if (!Construct(*this, BP, Fixture))
        return false;
    const auto Comparison = P4CompareOriginalClassNative(Fixture.Source.HeaderPath, Fixture.Source.BodyPath);
    const FString ComparisonText = P4OriginalClassComparisonJson(Comparison);
    FFileHelper::SaveStringToFile(ComparisonText, *(Fixture.Folder / TEXT("class-comparison.json")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (!TestTrue(TEXT("Actual source cook compares every held light field exactly, with native defaults readable"),
            Comparison.IsValid() && Comparison.Same && Comparison.ValuesKnown && Comparison.ComparedKnown &&
                Comparison.DefaultsReadable && Comparison.Differences == 0))
    {
        AddError(Comparison.ErrorDetail);
        for (const auto& Line : Comparison.OnlyGameSays)
            AddError(TEXT("Only source: ") + Line);
        for (const auto& Line : Comparison.OnlyProjectSays)
            AddError(TEXT("Only native cook: ") + Line);
        return false;
    }
    TSharedPtr<FJsonObject> ComparisonJson;
    if (!TestTrue(TEXT("Actual comparison publishes complete unsupported-field evidence"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(ComparisonText), ComparisonJson) &&
                ComparisonJson.IsValid() && !ComparisonJson->GetBoolField(TEXT("values_not_held_details_truncated")) &&
                Comparison.ValuesNotHeld == 9))
        return false;
    // Only the independently identified unresolved PP fields are permitted in
    // this partial capability gate. No light field or other new gap can hide
    // behind the comparison's legacy unsupported-value normalization.
    TSet<FString> PendingPP = {TEXT("Settings.bOverride_DepthOfFieldTiltAngles"),
        TEXT("Settings.bOverride_C4DepthOfFieldMask"), TEXT("Settings.bOverride_C4DepthOfFieldParam2"),
        TEXT("Settings.bOverride_C4PostProcessParam0"), TEXT("Settings.bOverride_DepthOfFieldMethod"),
        TEXT("Settings.C4DepthOfFieldMask"), TEXT("Settings.C4DepthOfFieldParam2"),
        TEXT("Settings.C4PostProcessParam0"), TEXT("Settings.DepthOfFieldMethod")};
    const auto& NotHeld = ComparisonJson->GetArrayField(TEXT("values_not_held_details"));
    if (!TestEqual(
            TEXT("Exactly nine known PP limitations remain; all 17 light values participate in native cook equality"),
            NotHeld.Num(), 9))
        return false;
    for (const auto& Item : NotHeld)
    {
        const auto Row = Item->AsObject();
        const FString Property = Row->GetStringField(TEXT("property"));
        if (!TestTrue(*FString::Printf(
                          TEXT("Only the exact unresolved PostProcess member may remain unsupported: %s"), *Property),
                Row->GetStringField(TEXT("object")) == TEXT("Default__BP_EnvController_C.PostProcess") &&
                    Row->GetStringField(TEXT("schema")) == TEXT("/Script/Engine.PostProcessSettings") &&
                    PendingPP.Remove(Property) == 1))
            return false;
    }
    if (!TestEqual(TEXT("Every known remaining PP limitation is explicitly accounted for"), PendingPP.Num(), 0))
        return false;
    auto Receipt = MakeShared<FJsonObject>();
    FString Hash;
    int64 Size = 0;
    if (!TestTrue(TEXT("Fresh actual Env package was saved"),
            P4HashFile(Fixture.AssetFile, 1024ll * 1024 * 1024, Size, Hash)))
        return false;
    Receipt->SetStringField(TEXT("owner"), TEXT("native-env-light-source/v1"));
    Receipt->SetStringField(TEXT("source_digest"), Fixture.Source.SourceDigest);
    Receipt->SetStringField(TEXT("asset_sha256"), Hash);
    Receipt->SetNumberField(TEXT("source_light_values"), 17);
    Receipt->SetNumberField(TEXT("native_light_fields"), 25);
    Receipt->SetBoolField(TEXT("full_class_closure"), false);
    Receipt->SetBoolField(TEXT("reference_only_policy_preserved"), true);
    Receipt->SetBoolField(TEXT("held_light_native_cook_exact"), true);
    Receipt->SetNumberField(TEXT("values_not_held"), Comparison.ValuesNotHeld);
    Receipt->SetArrayField(TEXT("remaining_post_process_values_not_held"), NotHeld);
    return TestTrue(TEXT("Retain source-bound first-process evidence for independent cold proof"),
               SaveReceipt(Fixture.Folder / TEXT("source-import.json"), Receipt)) &&
        !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualEnvLightColdConstruction,
    "P4MapKit.EngineDeclarations.ActualEnvLightColdConstruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualEnvLightColdConstruction::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ENV_LIGHT_FIXTURE")).IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Opt-in independent cold proof: use the same bounded fixture in a separate process."));
        return true;
    }
    using namespace P4ActualEnvLightTests;
    FFixture Fixture;
    if (!Fixture.Open(*this))
        return false;
    if (!TestNull(
            TEXT("Separate cold process begins with actual Env package unloaded"), FindPackage(nullptr, *Package)))
        return false;
    TSharedPtr<FJsonObject> Receipt;
    if (!TestTrue(TEXT("Cold proof requires the exact saved actual-source result"),
            ReadJson(Fixture.Folder / TEXT("source-import.json"), Receipt) &&
                Receipt->GetStringField(TEXT("owner")) == TEXT("native-env-light-source/v1") &&
                Receipt->GetStringField(TEXT("source_digest")) == Fixture.Source.SourceDigest &&
                HashMatches(Fixture.AssetFile, Receipt->GetStringField(TEXT("asset_sha256")))))
        return false;
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Object);
    const bool Good = Construct(*this, BP, Fixture);
    return Good &&
        TestTrue(TEXT("Cold load/construction never rewrites the source-derived saved Blueprint"),
            HashMatches(Fixture.AssetFile, Receipt->GetStringField(TEXT("asset_sha256")))) &&
        !HasAnyErrors();
}
#endif

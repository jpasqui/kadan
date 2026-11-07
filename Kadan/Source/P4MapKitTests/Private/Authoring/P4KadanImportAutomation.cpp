#include "P4KadanImport.h"
#include "P4KadanAssetPolicy.h"
#include "P4OriginalAssetImport.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "P4RecoveredFoliageType.h"
#include "P4ExportDriver.h"
#include "P4KadanContainerIndex.h"
#include "P4ExportProcess.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/Skeleton.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/blueprint_deep.h"
THIRD_PARTY_INCLUDES_END
#include "AssetRegistryModule.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectIterator.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "UObject/StructOnScope.h"

#include "P4KadanImportTestShared.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanContainerIndexTest, "P4MapKit.Kadan.ContainerIndex",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanContainerIndexTest::RunTest(const FString&)
{
    FP4KadanContainerIndex Index;
    FString Path, Error;
    const FString Actual = TEXT("Carrot4/Content/Carrot4/MasterMaterial/Texture/MDT_Base.uasset");
    const FString Row = TEXT("Carrot4-Switch 2b4b3cee530237e700000002 ExportBundleData ../../../") + Actual +
        TEXT("\n");
    TestTrue(TEXT("The measured installed retoc index layout is accepted"),
        Index.Read(TEXT("global 000000000000000000000007 LoaderInitialLoadMeta -\n") + Row + Row, Error));
    TestTrue(TEXT("A differently cased UE identity resolves its exact observed path"),
        Index.Resolve(TEXT("/GAME/CARROT4/MASTERMATERIAL/TEXTURE/mdt_base"), Path, Error));
    TestEqual(TEXT("The resolved path preserves the actual original spelling"), Path, Actual);
    TestFalse(TEXT("An absent package is refused rather than given a guessed filter"),
        Index.Resolve(TEXT("/Game/Carrot4/Absent"), Path, Error));
    TestTrue(TEXT("A missing package never leaves a previous successful path"), Path.IsEmpty());
    TestTrue(TEXT("A case-colliding index is read for explicit ambiguity diagnosis"),
        Index.Read(Row +
                TEXT(
                    "Carrot4-Switch abc ExportBundleData ../../../Carrot4/Content/carrot4/mastermaterial/texture/mdt_base.uasset\n"),
            Error));
    TestFalse(TEXT("Distinct observed paths for one UE identity are refused"),
        Index.Resolve(TEXT("/Game/Carrot4/MasterMaterial/Texture/MDT_Base"), Path, Error));
    TestTrue(TEXT("The refusal identifies ambiguity"), Error.Contains(TEXT("ambiguous")));
    TestFalse(TEXT("Traversal is refused before deriving an extraction destination"),
        Index.Read(TEXT("Carrot4-Switch abc ExportBundleData ../../../Carrot4/Content/../Injected.uasset\n"), Error));
    TestFalse(TEXT("A successful tool exit with no header/body does not count as acquisition"),
        P4KadanExtractedPackageReady(FPaths::ProjectSavedDir() / TEXT("P4MapKit/AbsentContainerOutput.uasset"),
            TEXT("/Game/Carrot4/Absent"), Error));
    const FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_HELD57_GAMEFILES"));
    if (!Game.IsEmpty())
    {
        TestTrue(TEXT("An actual converted source with a differently cased request retains identity"),
            P4KadanExtractedPackageReady(Game / Actual, TEXT("/GAME/CARROT4/MASTERMATERIAL/TEXTURE/mdt_base"), Error));
        TestFalse(TEXT("A complete actual source with the wrong identity is refused"),
            P4KadanExtractedPackageReady(Game / Actual, TEXT("/Game/Carrot4/OtherPackage"), Error));
    }
    return !HasAnyErrors();
}

// A package is converted from the containers once per project: the second import that needs it takes the
// kept copy (same bytes, no retoc run), and other containers or another retoc empty the cache.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanConvertedOriginalsTest, "P4MapKit.Kadan.ConvertedOriginalsAreKept",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanConvertedOriginalsTest::RunTest(const FString&)
{
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    FP4KadanImportSettings Settings;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    if (Settings.Retoc.IsEmpty() && !Root.IsEmpty())
        Settings.Retoc = FPaths::GetPath(FPaths::GetPath(Root)) / TEXT("private/retoc/retoc.exe");
    if (!TestTrue(TEXT("The test has the game's containers and retoc"),
            IFileManager::Get().DirectoryExists(*Settings.GamePaks) && IFileManager::Get().FileExists(*Settings.Retoc)))
        return false;
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Settings.Repak.IsEmpty() && !Settings.Retoc.IsEmpty())
        Settings.Repak = FPaths::GetPath(FPaths::GetPath(Settings.Retoc)) / TEXT("repak/repak.exe");
    const FString Work = FPaths::ProjectSavedDir() / TEXT("P4MapKit/Tests/ConvertedOriginals");
    IFileManager::Get().DeleteDirectory(*Work, false, true);
    Settings.ConvertedCache = Work / TEXT("cache");
    FP4KadanImportSettings UnsafeCache = Settings;
    UnsafeCache.ConvertedCache = Settings.GamePaks;
    TestTrue(TEXT("A source folder can never be cleared as a conversion cache"),
        P4KadanConversionCache(UnsafeCache).IsEmpty());
    FString Error;
    FP4KadanContainerIndex Index;
    bool IndexFromCache = false;
    if (!TestTrue(TEXT("The real container index is read"),
            P4KadanReadContainerIndex(Settings, Index, Error, &IndexFromCache)))
        return false;
    FP4KadanContainerIndex Reused;
    TestTrue(TEXT("The next import reuses its container listing"),
        P4KadanReadContainerIndex(Settings, Reused, Error, &IndexFromCache) && IndexFromCache);
    FP4KadanImportSettings Other = Settings;
    Other.Retoc = Work / TEXT("absent-retoc.exe");
    TestFalse(TEXT("Another retoc cannot reuse an unrelated container listing"),
        P4KadanReadContainerIndex(Other, Reused, Error, &IndexFromCache));
    TestFalse(TEXT("A failed listing is not a cache hit"), IndexFromCache);
    TestTrue(TEXT("A failed different source leaves the valid listing available"),
        P4KadanReadContainerIndex(Settings, Reused, Error, &IndexFromCache) && IndexFromCache);
    const FString Package = TEXT("/Game/Carrot4/MasterMaterial/Texture/MDT_Base");
    FString Path;
    if (!TestTrue(TEXT("The package is in the containers"), Index.Resolve(Package, Path, Error)))
        return false;
    bool FromCache = true;
    TestTrue(TEXT("The first conversion runs retoc. ") + Error,
        P4KadanConvertOriginal(Settings, Index, Package, Work / TEXT("a"), Error, &FromCache));
    TestFalse(TEXT("The first conversion is not from the cache"), FromCache);
    const double Started = FPlatformTime::Seconds();
    TestTrue(TEXT("The second conversion succeeds. ") + Error,
        P4KadanConvertOriginal(Settings, Index, Package, Work / TEXT("b"), Error, &FromCache));
    TestTrue(TEXT("The second conversion takes the kept copy"), FromCache);
    AddInfo(FString::Printf(TEXT("A kept package was placed in %.3f s."), FPlatformTime::Seconds() - Started));
    TArray<uint8> First, Second;
    TestTrue(TEXT("Both imports read the same converted bytes"),
        FFileHelper::LoadFileToArray(First, *(Work / TEXT("a") / Path)) &&
            FFileHelper::LoadFileToArray(Second, *(Work / TEXT("b") / Path)) && First.Num() > 0 && First == Second);
    TestTrue(TEXT("The package is ready for the importer where it was placed"),
        P4KadanExtractedPackageReady(Work / TEXT("b") / Path, Package, Error));
    // Another source (here: a changed record of it) empties the cache rather than serving its copy.
    FFileHelper::SaveStringToFile(TEXT("other containers"), *(Settings.ConvertedCache / TEXT("converted-from.txt")));
    TestEqual(TEXT("The cache is still where it was"), P4KadanConversionCache(Settings), Settings.ConvertedCache);
    TestFalse(TEXT("A cache from other containers is emptied"),
        IFileManager::Get().FileExists(*(Settings.ConvertedCache / Path)));
    TestTrue(TEXT("The package converts again. ") + Error,
        P4KadanConvertOriginal(Settings, Index, Package, Work / TEXT("c"), Error, &FromCache));
    TestFalse(TEXT("After the cache was emptied, retoc ran again"), FromCache);
    IFileManager::Get().DeleteDirectory(*Work, false, true);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanDeclarationTableTest, "P4MapKit.Kadan.DeclarationTables",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanDeclarationTableTest::RunTest(const FString&)
{
    const FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CAVE_GAME"));
    if (!TestTrue(TEXT("the validated original source union is configured"), !Game.IsEmpty()))
        return false;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    if (!TestTrue(TEXT("scan actual source headers"), P4KadanScanGame(Game, Sources, Problems)))
        return false;
    TMap<FName, TSharedPtr<FP4KadanSourceAsset>> ByPackage;
    for (const auto& Source : Sources)
        ByPackage.Add(FName(*Source->Package), Source);
    const TSet<FString> Roots = {
        TEXT("/Game/Carrot4/Core/UI/DT_UIWidgetStory"), TEXT("/Game/Carrot4/Core/Camera/Parameter/DT_CameraParameter")};
    const FP4KadanImportPlan Plan = P4KadanPlanImport(Sources, Roots, false);
    for (const FString& Problem : Plan.Problems)
        AddInfo(Problem);
    TestEqual(TEXT("typed class forward edges admit the actual original table plan"), Plan.Problems.Num(), 0);
    for (const FString& Package : Roots)
    {
        const FName Identity(*Package);
        const auto* Source = ByPackage.Find(Identity);
        TArray<FP4OriginalTableClassReference> Used;
        FString Error;
        if (!TestTrue(TEXT("actual table has complete used native class proof: ") + Package,
                Source && P4OriginalTableUsedClassReferences((*Source)->Header, (*Source)->Body, Used, Error) &&
                    Used.Num() && Error.IsEmpty()))
            return false;
        TArray<FP4OriginalTableClassReference> MissingClasses;
        for (const auto& Reference : Used)
        {
            const FString ClassPackage = FPackageName::ObjectPathToPackageName(Reference.ObjectPath);
            UClass* Existing = FindObject<UClass>(nullptr, *Reference.ObjectPath);
            if (!Existing && FPackageName::DoesPackageExist(ClassPackage))
                Existing = LoadObject<UClass>(nullptr, *Reference.ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
            if (!Existing)
            {
                MissingClasses.Add(Reference);
                continue;
            }
            const UBlueprint* Blueprint = Cast<UBlueprint>(Existing->ClassGeneratedBy);
            UClass* MetaClass = FindObject<UClass>(nullptr, *Reference.MetaClassPath);
            UMetaData* Metadata = Blueprint ? Blueprint->GetOutermost()->GetMetaData() : nullptr;
            TestTrue(
                TEXT(
                    "an already available target is the exact complete native generated class with its source MetaClass: ") +
                    Reference.Field,
                IsValid(Existing) &&
                    static_cast<const UObject*>(Existing)->IsA(UBlueprintGeneratedClass::StaticClass()) &&
                    IsValid(Blueprint) && Blueprint->IsAsset() && Blueprint->GeneratedClass == Existing &&
                    Blueprint->Status != BS_Error &&
                    P4ExactNativeString(Existing->GetPathName(), Reference.ObjectPath) &&
                    P4ExactNativeString(Existing->GetOutermost()->GetName(), ClassPackage) &&
                    Blueprint->GetOutermost() == Existing->GetOutermost() && Metadata &&
                    FString(Metadata->GetValue(Blueprint, TEXT("P4BlueprintStandIn"))) != TEXT("1") &&
                    FString(Metadata->GetValue(Blueprint, TEXT("P4BlueprintReconstructionIncomplete"))) != TEXT("1") &&
                    !Blueprint->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects) &&
                    IsValid(MetaClass) && MetaClass->HasAnyClassFlags(CLASS_Native) && Existing->IsChildOf(MetaClass));
        }
        const auto* References = Plan.DeclarationReferences.Find(Identity);
        if (MissingClasses.Num())
        {
            TestTrue(TEXT("the actual table waits for its missing generated class declarations: ") + Package,
                Plan.DeclarationDeferred.Contains(Identity));
            TestTrue(TEXT("the phase proof includes the complete missing class values: ") + Package,
                References && References->Num() == MissingClasses.Num());
            for (const auto& Reference : MissingClasses)
                TestTrue(
                    TEXT("every required used class leaf remains in the deferred source proof: ") + Reference.Field,
                    References &&
                        References->ContainsByPredicate(
                            [&](const FP4OriginalTableClassReference& Ref)
                            {
                                return Ref.Field == Reference.Field && Ref.ObjectPath == Reference.ObjectPath &&
                                    Ref.MetaClassPath == Reference.MetaClassPath;
                            }));
        }
        else
        {
            TestFalse(
                TEXT("a table whose exact native classes already exist does not need new declarations: ") + Package,
                Plan.DeclarationDeferred.Contains(Identity));
            TestTrue(TEXT("the ordinary planner adds no new class declaration proof for already available targets: ") +
                    Package,
                !References || !References->Num());
        }
        if (References)
            for (const auto& Reference : *References)
                TestTrue(TEXT("every used class retains a native MetaClass constraint"),
                    !Reference.MetaClassPath.IsEmpty() && !Reference.Field.IsEmpty());
    }
    const auto* Story = ByPackage.Find(TEXT("/Game/Carrot4/Core/UI/DT_UIWidgetStory"));
    if (!TestTrue(TEXT("original Story source exists"), Story != nullptr))
        return false;
    TArray<FP4OriginalTableClassReference> References;
    FString Error;
    TestFalse(TEXT("an unused target package is never admitted by a suffix guess"),
        P4OriginalTableDeclarationReferences(
            (*Story)->Header, (*Story)->Body, {TEXT("/Game/Carrot4/NoSuchUsedClass")}, References, Error));
    TestTrue(TEXT("unused target refusal is named"), !Error.IsEmpty());
    struct FExpectedUsedClass
    {
        const TCHAR* Table;
        const TCHAR* Field;
        const TCHAR* Object;
    };
    const FExpectedUsedClass Expected[] = {{TEXT("/Game/Carrot4/Core/Camera/Parameter/DT_CameraParameter"),
                                               TEXT(".Standard.LookOverParam.DroneParameter.DronePin"),
                                               TEXT("/Game/Carrot4/Placeables/Items/GDronePin.GDronePin_C")},
        {TEXT("/Game/Carrot4/Core/UI/DT_UIWidgetStory"), TEXT(".DemoMessage.Widget"),
            TEXT("/Game/Carrot4/UI/Common/Talk/WBP_ui_Talk_Text.WBP_ui_Talk_Text_C")}};
    for (const auto& Want : Expected)
    {
        const auto* Source = ByPackage.Find(FName(Want.Table));
        if (!TestTrue(TEXT("actual typed table discovery source exists"), Source != nullptr))
            return false;
        References.Reset();
        Error.Reset();
        if (!TestTrue(TEXT("native schema discovers actual used classes without an already selected Blueprint"),
                P4OriginalTableUsedClassReferences((*Source)->Header, (*Source)->Body, References, Error) &&
                    Error.IsEmpty()))
            return false;
        TestTrue(TEXT("the missing real class leaf joins the declaration proof"),
            References.ContainsByPredicate(
                [&](const FP4OriginalTableClassReference& Ref)
                {
                    return Ref.Field.EndsWith(Want.Field, ESearchCase::CaseSensitive) &&
                        Ref.ObjectPath == Want.Object && !Ref.MetaClassPath.IsEmpty();
                }));
        TSet<FName> Used;
        for (const auto& Ref : References)
        {
            TestTrue(
                TEXT("discovery returns complete native class constraints, never CDO objects or soft-path guesses"),
                !Ref.Field.IsEmpty() && !Ref.MetaClassPath.IsEmpty() && Ref.ObjectPath.StartsWith(TEXT("/Game/")) &&
                    !Ref.ObjectPath.Contains(TEXT("Default__")));
            Used.Add(FName(*FPackageName::ObjectPathToPackageName(Ref.ObjectPath)));
        }
        TArray<FP4OriginalTableClassReference> Proven;
        TestTrue(TEXT("discovered set still passes the unchanged complete declaration-only query"),
            Used.Num() &&
                P4OriginalTableDeclarationReferences((*Source)->Header, (*Source)->Body, Used, Proven, Error) &&
                Proven.Num() == References.Num());
        Used.Add(TEXT("/Game/Carrot4/NoSuchUsedClass"));
        TestFalse(TEXT("adding an unused import guess cannot pass the strict used-class proof"),
            P4OriginalTableDeclarationReferences((*Source)->Header, (*Source)->Body, Used, Proven, Error));
        TestTrue(TEXT("unused declaration proof is explicitly refused"),
            Error.Contains(TEXT("no used native class-typed reference")));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanDeclarationTableCohortTest, "P4MapKit.Kadan.DeclarationTableCohort",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanDeclarationTableCohortTest::RunTest(const FString&)
{
    const FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CAVE_GAME"));
    const FString Report = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_DECLARATION_TABLE_COHORT"));
    const FString Expected = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_DECLARATION_TABLE_COHORT_SHA256"));
    int64 Bytes = 0;
    FString Actual, Text;
    if (!TestTrue(TEXT("the exact retained 2647-row warm baseline is explicitly configured"),
            !Game.IsEmpty() && Expected.Len() == 64 && P4HashFile(Report, 32 * 1024 * 1024, Bytes, Actual) &&
                P4ExactNativeString(Actual, Expected) && FFileHelper::LoadFileToString(Text, *Report)))
        return false;
    TSharedPtr<FJsonObject> Baseline;
    const TSharedPtr<FJsonObject>* Rows = nullptr;
    if (!TestTrue(TEXT("the retained report has the complete original asset cohort"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Baseline) && Baseline.IsValid() &&
                Baseline->TryGetObjectField(TEXT("assets"), Rows) && (*Rows)->Values.Num() == 2647))
        return false;
    TSet<FName> Tables;
    for (const auto& Pair : (*Rows)->Values)
    {
        const auto Row = Pair.Value->AsObject();
        FString Kind;
        bool ReferenceOnly = false;
        if (Row.IsValid() && Row->TryGetStringField(TEXT("kind"), Kind) && Kind == TEXT("DataTable") &&
            Row->TryGetBoolField(TEXT("reference_only"), ReferenceOnly) && ReferenceOnly)
            Tables.Add(FName(*Pair.Key));
    }
    if (!TestEqual(TEXT("all sixty retained original DataTables are selected"), Tables.Num(), 60))
        return false;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    if (!TestTrue(TEXT("the actual converted source union is readable"), P4KadanScanGame(Game, Sources, Problems)))
        return false;
    TMap<FName, TSharedPtr<FP4KadanSourceAsset>> ByPackage;
    for (const auto& Source : Sources)
        ByPackage.Add(FName(*Source->Package), Source);
    TSet<FName> UsedRoots;
    TMap<FString, int32> Kinds;
    int32 ClassFree = 0, Fields = 0;
    for (const FName Identity : Tables)
    {
        const auto* Source = ByPackage.Find(Identity);
        if (!TestTrue(TEXT("actual cohort source exists: ") + Identity.ToString(),
                Source && (*Source)->Kind == TEXT("DataTable")))
            continue;
        TArray<FP4OriginalTableClassReference> References;
        FString Error;
        if (!TestTrue(TEXT("actual cohort native class discovery: ") + Identity.ToString() + TEXT(" ") + Error,
                P4OriginalTableUsedClassReferences((*Source)->Header, (*Source)->Body, References, Error)))
        {
            AddInfo(Error);
            continue;
        }
        if (!References.Num())
            ++ClassFree;
        TSet<FName> UsedHere;
        for (const auto& Ref : References)
        {
            ++Fields;
            const FName Package(*FPackageName::ObjectPathToPackageName(Ref.ObjectPath));
            UsedHere.Add(Package);
            TestTrue(TEXT("every discovered value retains exact native class-property custody"),
                !Ref.Field.IsEmpty() && !Ref.MetaClassPath.IsEmpty() && Ref.ObjectPath.StartsWith(TEXT("/Game/")) &&
                    !Ref.ObjectPath.Contains(TEXT("Default__")));
            if (!UsedRoots.Contains(Package))
            {
                UsedRoots.Add(Package);
                const auto* ClassSource = ByPackage.Find(Package);
                const FString Kind = ClassSource ? (*ClassSource)->Kind : TEXT("source-not-in-test-union");
                ++Kinds.FindOrAdd(Kind);
                AddInfo(TEXT("used table class root ") + Package.ToString() + TEXT(" kind=") + Kind);
            }
        }
        if (UsedHere.Num())
        {
            TArray<FP4OriginalTableClassReference> Strict;
            TestTrue(TEXT("each used class set still passes complete declaration-only proof"),
                P4OriginalTableDeclarationReferences((*Source)->Header, (*Source)->Body, UsedHere, Strict, Error) &&
                    Strict.Num() == References.Num());
            UsedHere.Add(TEXT("/Game/Carrot4/NoSuchUsedClass"));
            TestFalse(TEXT("unused-import guessing never authorizes another class"),
                P4OriginalTableDeclarationReferences((*Source)->Header, (*Source)->Body, UsedHere, Strict, Error));
        }
    }
    AddInfo(
        FString::Printf(TEXT("DataTable cohort=%d empty-used-set=%d used-fields=%d unique-generated-class-roots=%d"),
            Tables.Num(), ClassFree, Fields, UsedRoots.Num()));
    for (const auto& Pair : Kinds)
        AddInfo(FString::Printf(TEXT("used class root kind %s: %d"), *Pair.Key, Pair.Value));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanImportPlanTest, "P4MapKit.Kadan.ImportPlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanImportPlanTest::RunTest(const FString&)
{
    auto Source = [](const TCHAR* Leaf, const TCHAR* Kind, int64 Bytes)
    {
        auto Asset = MakeShared<FP4KadanSourceAsset>();
        Asset->Package = FString(TEXT("/Game/__P4KadanPlannerTest/")) + Leaf;
        Asset->Kind = Kind;
        Asset->SourceBytes = Bytes;
        return Asset;
    };
    const auto Texture = Source(TEXT("Texture"), TEXT("Texture2D"), 7);
    const auto First = Source(TEXT("First"), TEXT("MaterialInstanceConstant"), 13);
    const auto Second = Source(TEXT("Second"), TEXT("MaterialInstanceConstant"), 19);
    First->Dependencies.Add(Texture->Package);
    Second->Dependencies.Add(Texture->Package);
    TSet<FString> Roots;
    Roots.Add(First->Package);
    Roots.Add(Second->Package);
    const FP4KadanImportPlan Shared = P4KadanPlanImport({Second, Texture, First}, Roots, true);
    TestEqual(TEXT("Explicit roots counted separately"), Shared.Roots, 2);
    TestEqual(TEXT("Shared dependency included once"), Shared.Dependencies, 1);
    TestEqual(TEXT("Shared dependency reported"), Shared.SharedDependencies, 1);
    TestEqual(TEXT("Physical source lengths counted once"), Shared.SourceBytes, int64(39));
    TestEqual(TEXT("New import source estimate"), Shared.RemainingSourceBytes, int64(39));
    TestEqual(TEXT("Three unique packages"), Shared.Assets.Num(), 3);
    TestTrue(TEXT("Prerequisite precedes material instances"), Shared.Assets.Num() && Shared.Assets[0] == Texture);
    TestEqual(TEXT("Supported chain admitted"), Shared.Problems.Num(), 0);

    const auto CaseTexture = Source(TEXT("demo/T_Case"), TEXT("Texture2D"), 5);
    const auto CaseMaster = Source(TEXT("demo/M_Case"), TEXT("Material"), 11);
    CaseMaster->Dependencies = {CaseTexture->Package.ToUpper(), CaseTexture->Package};
    TSet<FString> CaseRoots;
    CaseRoots.Add(CaseMaster->Package.ToUpper());
    CaseRoots.Add(CaseMaster->Package);
    const FP4KadanImportPlan CasePlan = P4KadanPlanImport({CaseMaster, CaseTexture}, CaseRoots, false);
    TestEqual(TEXT("Differently cased selected roots are one UE package identity"), CasePlan.Roots, 1);
    TestEqual(TEXT("Differently cased hard dependency resolves without a missing-package problem"),
        CasePlan.Problems.Num(), 0);
    TestEqual(
        TEXT("Differently cased repeated hard references include each actual package once"), CasePlan.Assets.Num(), 2);
    TestEqual(
        TEXT("Physical source bytes are counted once for each original identity"), CasePlan.SourceBytes, int64(16));
    TestEqual(TEXT("The actual dependency is counted once"), CasePlan.Dependencies, 1);
    TestTrue(TEXT("Case-insensitive planning preserves actual source strings and prerequisite order"),
        CasePlan.Assets.Num() == 2 && CasePlan.Assets[0] == CaseTexture && CasePlan.Assets[1] == CaseMaster &&
            CasePlan.Assets[0]->Package == TEXT("/Game/__P4KadanPlannerTest/demo/T_Case") &&
            CasePlan.Assets[1]->Package == TEXT("/Game/__P4KadanPlannerTest/demo/M_Case"));

    const auto Movie = Source(TEXT("MDT_Movie"), TEXT("MediaTexture"), 3);
    const auto MovieMaster = Source(TEXT("M_Movie"), TEXT("Material"), 5);
    MovieMaster->Dependencies.Add(Movie->Package);
    TSet<FString> MovieRoots;
    MovieRoots.Add(MovieMaster->Package);
    const FP4KadanImportPlan MoviePlan = P4KadanPlanImport({MovieMaster, Movie}, MovieRoots, false);
    TestEqual(TEXT("A real media-texture kind is a material prerequisite"), MoviePlan.Problems.Num(), 0);
    TestTrue(TEXT("Movie texture is planned before the master that samples it"),
        MoviePlan.Assets.Num() == 2 && MoviePlan.Assets[0] == Movie && MoviePlan.Assets[1] == MovieMaster);

    const auto SoftCase = Source(TEXT("SoftCase"), TEXT("Blueprint"), 17);
    SoftCase->Dependencies = {CaseTexture->Package};
    SoftCase->SoftDependencies = {CaseTexture->Package.ToUpper()};
    TSet<FString> SoftCaseRoots;
    SoftCaseRoots.Add(SoftCase->Package);
    const FP4KadanImportPlan SoftCasePlan = P4KadanPlanImport({SoftCase, CaseTexture}, SoftCaseRoots, false);
    TestTrue(TEXT("A differently cased soft path does not become a hard import edge"),
        SoftCasePlan.Assets.Num() == 1 && SoftCasePlan.Assets[0] == SoftCase && SoftCasePlan.SourceBytes == int64(17));
    // Unreal package identities are FNames; retained spelling and disk paths
    // must remain the original source spelling when references differ in case.
    TSet<FString> AliasRoots;
    AliasRoots.Add(First->Package.ToLower());
    AliasRoots.Add(First->Package);
    First->Dependencies = {Texture->Package.ToUpper()};
    const FP4KadanImportPlan Alias = P4KadanPlanImport({Texture, First}, AliasRoots, true);
    TestEqual(TEXT("Case aliases count as one selected package"), Alias.Roots, 1);
    TestEqual(TEXT("Case-varied hard reference finds its actual source"), Alias.Assets.Num(), 2);
    TestEqual(TEXT("Case aliases retain original source objects and spelling"),
        Alias.Assets.Num() ? Alias.Assets[0]->Package : FString(), Texture->Package);
    TestEqual(TEXT("Case-varied dependency produces no missing-source warning"), Alias.Warnings.Num(), 0);
    First->Dependencies = {Texture->Package};

    TSet<FString> UnknownRoots;
    UnknownRoots.Add(TEXT("/Game/__P4KadanPlannerTest/Missing"));
    const FP4KadanImportPlan Missing = P4KadanPlanImport(TArray<TSharedPtr<FP4KadanSourceAsset>>(), UnknownRoots, true);
    TestEqual(TEXT("Missing root refused"), Missing.Problems.Num(), 1);
    TestEqual(TEXT("Missing root does not produce negative dependency count"), Missing.Dependencies, 0);

    const auto Left = Source(TEXT("CycleLeft"), TEXT("DataAsset"), 3);
    const auto Right = Source(TEXT("CycleRight"), TEXT("DataAsset"), 5);
    Left->Dependencies.Add(Right->Package);
    Right->Dependencies.Add(Left->Package);
    TSet<FString> CycleRoots;
    CycleRoots.Add(Left->Package);
    TestTrue(TEXT("Generic cycle is an explicit refusal"),
        P4KadanPlanImport({Left, Right}, CycleRoots, true).Problems.Num() > 0);
    Left->Kind = Right->Kind = TEXT("Blueprint");
    TestEqual(TEXT("Blueprint cycle delegates to native predeclaration"),
        P4KadanPlanImport({Left, Right}, CycleRoots, true).Problems.Num(), 0);
    Left->Kind = Right->Kind = TEXT("DataAsset");
    Right->SoftDependencies.Add(Left->Package);
    TestEqual(TEXT("A cycle through a soft reference is allowed"),
        P4KadanPlanImport({Left, Right}, CycleRoots, true).Problems.Num(), 0);
    TSet<FString> OtherRoots;
    OtherRoots.Add(Right->Package);
    TestEqual(TEXT("from either end"), P4KadanPlanImport({Left, Right}, OtherRoots, true).Problems.Num(), 0);

    Right->Kind = TEXT("Blueprint");
    Right->Dependencies.Empty();
    Right->SoftDependencies.Empty();
    const FP4KadanImportPlan Forward = P4KadanPlanImport({Left, Right}, CycleRoots, true);
    TestTrue(TEXT("Later importer dependency needs native predeclaration"),
        Forward.Problems.ContainsByPredicate(
            [](const FString& Problem) { return Problem.Contains(TEXT("imports later")); }));
    TestFalse(TEXT("A sourceless forward reference never gains a declaration phase"),
        Forward.DeclarationDeferred.Contains(FName(*Left->Package)));
    TestFalse(TEXT("A sourceless forward reference never gains native class proof"),
        Forward.DeclarationReferences.Contains(FName(*Left->Package)));

    // A map names its streaming levels and the classes its placement lists place by soft references.
    const auto Map = Source(TEXT("Map"), TEXT("World"), 11);
    const auto Sublevel = Source(TEXT("Sublevel"), TEXT("World"), 17);
    const auto Placements = Source(TEXT("Placements"), TEXT("ActorPlacementInfoAsset"), 2);
    const auto Placed = Source(TEXT("Placed"), TEXT("Blueprint"), 23);
    const auto Lighting = Source(TEXT("Map_BuiltData"), TEXT("MapBuildDataRegistry"), 29);
    const FString Absent = TEXT("/Game/__P4KadanPlannerTest/Absent");
    Map->Dependencies = {Lighting->Package, Placements->Package, Sublevel->Package};
    Map->SoftDependencies = {Placements->Package, Sublevel->Package};
    Sublevel->Dependencies = {Map->Package};
    Sublevel->SoftDependencies = {Map->Package};
    Placements->Dependencies = {Absent, Placed->Package};
    Placements->SoftDependencies = {Absent, Placed->Package};
    TSet<FString> MapRoots;
    MapRoots.Add(Map->Package);
    const FP4KadanImportPlan Soft = P4KadanPlanImport({Map, Sublevel, Placements, Placed, Lighting}, MapRoots, true);
    TArray<FString> Planned;
    for (const auto& Asset : Soft.Assets)
        Planned.Add(Asset->Package);
    TestTrue(TEXT("A map brings its streaming level"), Planned.Contains(Sublevel->Package));
    TestTrue(TEXT("and the class its placement list names by a soft path"), Planned.Contains(Placed->Package));
    TestEqual(
        TEXT("A soft reference to a later importer and a cycle of levels are no problem"), Soft.Problems.Num(), 0);
    TestTrue(TEXT("A soft reference to a package that is nowhere is a warning"),
        Soft.Warnings.Num() == 1 && Soft.Warnings[0].Contains(Absent));
    // The level's built lighting is imported, as the game's editor held it, before the level that names it.
    TestTrue(TEXT("The level's built lighting is imported"),
        Planned.Contains(Lighting->Package) && Planned.Find(Lighting->Package) < Planned.Find(Map->Package));
    TestEqual(TEXT("and is no failure"), Soft.Problems.Num(), 0);
    TestTrue(TEXT("Levels come last"),
        Planned.Num() == 5 && Soft.Assets[3]->Kind == TEXT("World") && Soft.Assets[4]->Kind == TEXT("World"));

    // What a placed class needs by hard reference comes in before it. What it names only by soft
    // reference does not, and an animation comes in only when something imported requires it.
    const auto Body = Source(TEXT("Body"), TEXT("SkeletalMesh"), 1);
    const auto Physics = Source(TEXT("Body_Physics"), TEXT("PhysicsAsset"), 1);
    const auto Skin = Source(TEXT("Skin"), TEXT("MaterialInstanceConstant"), 1);
    const auto Master = Source(TEXT("Master"), TEXT("Material"), 1);
    const auto Function = Source(TEXT("Function"), TEXT("MaterialFunction"), 1);
    const auto Walk = Source(TEXT("Walk"), TEXT("AnimSequence"), 1);
    const auto Idle = Source(TEXT("Idle"), TEXT("AnimSequence"), 1);
    const auto Table = Source(TEXT("Table"), TEXT("DataTable"), 1);
    const auto Animation = Source(TEXT("Animation"), TEXT("AnimBlueprint"), 1);
    Placed->Dependencies = {Animation->Package, Body->Package, Idle->Package, Table->Package};
    Placed->SoftDependencies = {Idle->Package, Table->Package};
    Animation->Dependencies = {Walk->Package};
    Body->Dependencies = {Physics->Package, Skin->Package};
    Skin->Dependencies = {Master->Package};
    Master->Dependencies = {Function->Package};
    const FP4KadanImportPlan Placing = P4KadanPlanImport({Map, Sublevel, Placements, Placed, Lighting, Body, Physics,
                                                             Skin, Master, Function, Walk, Idle, Table, Animation},
        MapRoots, true);
    TArray<FString> Order;
    for (const auto& Asset : Placing.Assets)
        Order.Add(Asset->Package);
    TestEqual(TEXT("A placed class's hard references plan without problems"), Placing.Problems.Num(), 0);
    for (const auto& Needed : {Body, Physics, Skin, Master, Function, Animation, Walk})
        TestTrue(TEXT("A placed class brings what it needs by hard reference: ") + Needed->Package,
            Order.Contains(Needed->Package));
    TestFalse(TEXT("but not an animation it names only by soft reference"), Order.Contains(Idle->Package));
    TestFalse(TEXT("nor a table it names only by soft reference"), Order.Contains(Table->Package));
    TestTrue(TEXT("The skeletal mesh, its physics and its materials come before the class"),
        Order.Find(Body->Package) < Order.Find(Placed->Package) &&
            Order.Find(Physics->Package) < Order.Find(Body->Package) &&
            Order.Find(Skin->Package) < Order.Find(Body->Package) &&
            Order.Find(Function->Package) < Order.Find(Master->Package));

    // A broken source stops a strict import before dependent or unrelated packages are attempted.
    // A separate real-source run checks successful native import and the finished report.
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty())
    {
        AddError(TEXT("The import run part of this test needs P4_NATIVE_ROOT."));
        return false;
    }
    const FString GridPackage = TEXT(
        "/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/GridInfo/Cave000_F00_P_FoliageGridData");
    const auto Grid = MakeShared<FP4KadanSourceAsset>();
    Grid->Package = GridPackage;
    Grid->Kind = TEXT("FoliageGridDataAsset");
    Grid->Header = Root /
        TEXT(
            "host/Saved/P4MapKit/FullNativeCave000/game/Carrot4/Content/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/GridInfo/Cave000_F00_P_FoliageGridData.uasset");
    Grid->Body = FPaths::ChangeExtension(Grid->Header, TEXT("uexp"));
    if (!TestTrue(
            TEXT("The game's grid data file is where the test looks"), IFileManager::Get().FileExists(*Grid->Header)))
        return false;
    FString GridSourceBefore, GridSourceError, GridNativeClass;
    if (!TestTrue(TEXT("The exact original grid header/body and native root are verified"),
            P4KadanRequestedSourceDigest(*Grid, GridSourceBefore, GridSourceError, &GridNativeClass) &&
                GridNativeClass == TEXT("/Script/Carrot4.FoliageGridDataAsset")))
    {
        AddError(GridSourceError);
        return false;
    }
    AddInfo(TEXT("Original grid source SHA256: ") + GridSourceBefore);
    P4KadanImportTests::FMade Made(TEXT("KadanImportPlan"));
    if (!TestTrue(TEXT("the test can keep its exact import cleanup list"), Made.Ready))
        return false;
    const FString GridObject = GridPackage + TEXT(".") + FPackageName::GetShortName(GridPackage);
    if (!TestTrue(TEXT("The real grid destination is absent before either import"),
            !FPackageName::DoesPackageExist(GridPackage) && !FindObject<UObject>(nullptr, *GridObject)))
        return false;
    const auto Broken = Source(TEXT("Broken"), TEXT("DataAsset"), 1);
    Broken->Header = Root / TEXT("no-such-folder/Broken.uasset");
    Broken->Body = FPaths::ChangeExtension(Broken->Header, TEXT("uexp"));
    const auto Needs = Source(TEXT("Needs"), TEXT("DataAsset"), 1);
    Needs->Header = Broken->Header;
    Needs->Body = Broken->Body;
    Needs->Dependencies = {Broken->Package};
    TSet<FString> RunRoots;
    RunRoots.Add(Needs->Package);
    RunRoots.Add(GridPackage);
    const FP4KadanImportPlan RunPlan = P4KadanPlanImport({Broken, Needs, Grid}, RunRoots, true);
    TestEqual(TEXT("The run's plan has no problems"), RunPlan.Problems.Num(), 0);
    // The report is written at most every few seconds while the import goes; the finished import's saved
    // report still holds every asset's reply and where the time went.
    const double Interval = P4KadanReportInterval;
    P4KadanReportInterval = 3600;
    const FP4KadanImportResult Run = P4KadanRunImport(FP4KadanImportSettings(), RunPlan, true);
    {
        FString Text;
        TSharedPtr<FJsonObject> Saved;
        const TSharedPtr<FJsonObject>* Replies = nullptr;
        const TSharedPtr<FJsonObject>* Timing = nullptr;
        TestTrue(TEXT("The strict stop saves the exact finished report and only the attempted asset"),
            FFileHelper::LoadFileToString(Text, *(FPaths::ProjectSavedDir() / TEXT("Kadan/import-result.json"))) &&
                P4ExactNativeString(Text, Run.Report) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Saved) && Saved.IsValid() &&
                Saved->TryGetObjectField(TEXT("assets"), Replies) && (*Replies)->Values.Num() == 1 &&
                (*Replies)->HasField(Broken->Package) && !(*Replies)->HasField(Needs->Package) &&
                !(*Replies)->HasField(GridPackage));
        const TSharedPtr<FJsonObject>* BrokenReply = nullptr;
        TestTrue(TEXT("The attempted package reports its actual unreadable source"),
            Replies && (*Replies)->TryGetObjectField(Broken->Package, BrokenReply) &&
                !(*BrokenReply)->GetBoolField(TEXT("valid")) &&
                (*BrokenReply)->GetStringField(TEXT("error")) == TEXT("asset-import-source-unreadable"));
        TestTrue(TEXT("The strict stop report tells where the time went"),
            Saved.IsValid() && Saved->TryGetObjectField(TEXT("seconds"), Timing) && (*Timing)->HasField(TEXT("total")));
    }
    TestFalse(TEXT("A run with a failure is not ok"), Run.Ok);
    TestTrue(TEXT("Only the attempted broken package is recorded as refused"),
        Run.NotImported.Num() == 1 && Run.NotImported.Contains(Broken->Package) &&
            !Run.NotImported.Contains(Needs->Package));
    TestTrue(TEXT("The strict source refusal names the exact package and cause"),
        Run.Problems.Num() == 1 && Run.Problems[0].StartsWith(Broken->Package) &&
            Run.Problems[0].Contains(TEXT("asset-import-source-unreadable")));
    TestTrue(TEXT("A strict source failure does not claim later imports or reference-only assets"),
        Run.Imported.Num() == 0 && Run.ReferenceOnly.Num() == 0);
    TestTrue(TEXT("The unrelated real grid remains absent after the strict stop"),
        !FPackageName::DoesPackageExist(GridPackage) && !FindObject<UObject>(nullptr, *GridObject));

    const FP4KadanImportPlan GridPlan = P4KadanPlanImport({Grid}, {GridPackage}, true);
    TestEqual(TEXT("The independent real-source grid plan has no problems"), GridPlan.Problems.Num(), 0);
    const FP4KadanImportResult GridRun = P4KadanRunImport(FP4KadanImportSettings(), GridPlan, true);
    P4KadanReportInterval = Interval;
    TestTrue(TEXT("The independent original grid imports successfully"),
        GridRun.Ok && GridRun.Problems.Num() == 0 && GridRun.NotImported.Num() == 0 && GridRun.Imported.Num() == 1 &&
            GridRun.Imported.Contains(GridPackage));
    // Native grid-tail custody now preserves the cells through cooking; this is no longer the old
    // unsupported-tail reference-only fixture. The actual native comparison must remain complete.
    TestFalse(
        TEXT("The fully recovered native grid is not reference-only"), GridRun.ReferenceOnly.Contains(GridPackage));
    {
        FString Text;
        TSharedPtr<FJsonObject> Saved;
        const TSharedPtr<FJsonObject>* Replies = nullptr;
        const TSharedPtr<FJsonObject>* Timing = nullptr;
        const TSharedPtr<FJsonObject>* GridReply = nullptr;
        TestTrue(TEXT("The successful saved report is the exact finished grid import's"),
            FFileHelper::LoadFileToString(Text, *(FPaths::ProjectSavedDir() / TEXT("Kadan/import-result.json"))) &&
                P4ExactNativeString(Text, GridRun.Report) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Saved) && Saved.IsValid() &&
                Saved->GetBoolField(TEXT("ok")) && Saved->TryGetObjectField(TEXT("assets"), Replies) &&
                (*Replies)->Values.Num() == 1 && (*Replies)->TryGetObjectField(GridPackage, GridReply));
        TestTrue(TEXT("The independent grid report tells where the time went"),
            Saved.IsValid() && Saved->TryGetObjectField(TEXT("seconds"), Timing) && (*Timing)->HasField(TEXT("total")));
        double Compared = -1;
        FString NativeError;
        TestTrue(TEXT("The grid has a saved complete native comparison at its original identity"),
            GridReply && (*GridReply)->GetBoolField(TEXT("valid")) && (*GridReply)->GetBoolField(TEXT("created")) &&
                (*GridReply)->GetBoolField(TEXT("saved")) && !(*GridReply)->GetBoolField(TEXT("reference_only")) &&
                (*GridReply)->GetStringField(TEXT("package")) == GridPackage &&
                (*GridReply)->GetStringField(TEXT("asset")) == GridObject &&
                (*GridReply)->TryGetNumberField(TEXT("compared"), Compared) && Compared > 0 &&
                (*GridReply)->TryGetStringField(TEXT("error"), NativeError) && NativeError.IsEmpty());
    }
    FString GridSourceAfter;
    TestTrue(TEXT("Both runs preserve the exact original grid source bytes"),
        P4KadanRequestedSourceDigest(*Grid, GridSourceAfter, GridSourceError) &&
            P4ExactNativeString(GridSourceBefore, GridSourceAfter));
    return !HasAnyErrors();
}
#endif

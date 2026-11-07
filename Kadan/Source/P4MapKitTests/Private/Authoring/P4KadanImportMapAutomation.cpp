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
#include "P4KadanImportTestShared.h"

// Cave000_F00 through the panel's importer, from the game's packages into a project without them, with the
// counts the earlier whole-map import reached. Needs the extracted map with the game's shader library and
// parameter collections, below P4_NATIVE_ROOT.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanImportMapCave000Test, "P4MapKit.Kadan.ImportMapCave000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanImportMapCave000Test::RunTest(const FString&)
{
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty())
    {
        AddError(TEXT("The Cave000 import test needs P4_NATIVE_ROOT."));
        return false;
    }
    const FString Repo = FPaths::GetPath(FPaths::GetPath(Root));
    FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CAVE_GAME"));
    if (Game.IsEmpty())
        Game = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    const FString Map = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");
    for (const FString& Needed : {Game})
        if (!TestTrue(TEXT("The test's input is there: ") + Needed, IFileManager::Get().DirectoryExists(*Needed)))
            return false;
    P4KadanImportTests::FMade Made(TEXT("KadanImportMapCave000"));
    if (!TestTrue(TEXT("the test can keep its exact import cleanup list"), Made.Ready))
        return false;
    FString MapFile;
    FPackageName::TryConvertLongPackageNameToFilename(Map, MapFile, TEXT(".umap"));
    if (!TestFalse(TEXT("The project has no Cave000 before the test"), IFileManager::Get().FileExists(*MapFile)))
        return false;

    const FString DefaultsBefore = WaterboxDefaultMeasurements(TEXT("before_import"));
    const double Start = FPlatformTime::Seconds();
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Game;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    if (Settings.Retoc.IsEmpty())
        Settings.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Settings.Repak.IsEmpty() && !Settings.Retoc.IsEmpty())
        Settings.Repak = FPaths::GetPath(FPaths::GetPath(Settings.Retoc)) / TEXT("repak/repak.exe");
    if (!TestTrue(TEXT("Original containers are configured"), IFileManager::Get().DirectoryExists(*Settings.GamePaks)))
        return false;
    FP4KadanImportResult Result;
    {
        FRow13CompilerCapture CompilerCapture;
        Result = P4KadanImportMap(Settings, Map, true);
        if (!CompilerCapture.Save(*this))
            return false;
    }
    if (!KeepImportTestReport(*this, TEXT("Cave000ImportMap"), Result))
        return false;
    const FString Defaults = DefaultsBefore + WaterboxDefaultMeasurements(TEXT("after_import"));
    const FString DefaultsFile = FPaths::ProjectSavedDir() /
        TEXT("P4MapKit/RecoveryImports/Cave000WaterboxDefaults.txt");
    if (!TestTrue(TEXT("Keep actual fresh SDK standalone, CDO and archetype defaults"),
            FFileHelper::SaveStringToFile(Defaults, *DefaultsFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
        return false;
    AddInfo(Defaults);
    // Required class references exist in the original containers even when the
    // selected extracted folder omits them. They must join the normal plan, not
    // disappear as successful null assignments during component import.
    const FString CrackAnimation = TEXT("/Game/Carrot4/Objects/Gimmicks/CrackPotL/Animation/ABP_CrackPot");
    const FString BabyAnimation = TEXT("/Game/Carrot4/Characters/Teki/Baby/ABP_Baby");
    auto HasStandInMarker = [](UObject* Asset)
    {
        return Asset &&
            FString(Asset->GetOutermost()->GetMetaData()->GetValue(Asset, TEXT("P4BlueprintStandIn"))) == TEXT("1");
    };
    UAnimBlueprint* CrackBlueprint = nullptr;
    for (const FString& Name : {CrackAnimation, BabyAnimation})
    {
        TestTrue(TEXT("Normal import includes the actually required animation Blueprint: ") + Name,
            Result.Imported.Contains(Name));
        UAnimBlueprint* Animation = LoadObject<UAnimBlueprint>(nullptr,
            *(Name + TEXT(".") + FPackageName::GetLongPackageAssetName(Name)), nullptr, LOAD_NoWarn | LOAD_Quiet);
        TestTrue(TEXT("Required animation class is real and compiled: ") + Name,
            Animation && Animation->GeneratedClass && !HasStandInMarker(Animation));
        TestTrue(TEXT("Required animation skeleton has actual source bones: ") + Name,
            Animation && Animation->TargetSkeleton &&
                Animation->TargetSkeleton->GetReferenceSkeleton().GetRawBoneNum() > 0 &&
                !HasStandInMarker(Animation->TargetSkeleton));
        if (Name == CrackAnimation)
            CrackBlueprint = Animation;
    }
    UBlueprint* Pot = LoadObject<UBlueprint>(nullptr,
        TEXT("/Game/Carrot4/Placeables/Gimmicks/CrackPot/GCrackPotS.GCrackPotS"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    UBlueprintGeneratedClass* PotParent = Pot ? Cast<UBlueprintGeneratedClass>(Pot->ParentClass) : nullptr;
    USCS_Node* MeshNode = PotParent && PotParent->SimpleConstructionScript
        ? PotParent->SimpleConstructionScript->FindSCSNode(FName(TEXT("CarrotSkeletalMesh")))
        : nullptr;
    UInheritableComponentHandler* Handler = Pot ? Pot->GetInheritableComponentHandler(false) : nullptr;
    UActorComponent* Template = Handler && MeshNode ? Handler->GetOverridenComponentTemplate(FComponentKey(MeshNode))
                                                    : nullptr;
    FObjectPropertyBase* AnimationProperty = Template
        ? FindFProperty<FObjectPropertyBase>(Template->GetClass(), TEXT("AnimClass"))
        : nullptr;
    TestTrue(TEXT("The source child template survives compilation with its actual required animation class"),
        Template && AnimationProperty && CrackBlueprint && CrackBlueprint->GeneratedClass &&
            AnimationProperty->GetObjectPropertyValue_InContainer(Template) == CrackBlueprint->GeneratedClass);
    TMap<FString, int32> Kinds;
    int32 Levels = 0, Textures = 0, Meshes = 0, Materials = 0, MaterialInstances = 0, Blueprints = 0, Others = 0,
          Collections = 0;
    for (const auto& Pair : Result.Imported)
    {
        ++Kinds.FindOrAdd(Pair.Value);
        if (Pair.Value == TEXT("World"))
            ++Levels;
        else if (Pair.Value.StartsWith(TEXT("Texture")))
            ++Textures;
        else if (Pair.Value == TEXT("StaticMesh"))
            ++Meshes;
        else if (Pair.Value == TEXT("Material"))
            ++Materials;
        else if (Pair.Value == TEXT("MaterialInstanceConstant"))
            ++MaterialInstances;
        else if (Pair.Value.EndsWith(TEXT("Blueprint")))
            ++Blueprints;
        else if (Pair.Value == TEXT("MaterialParameterCollection"))
            ++Collections;
        else
            ++Others;
    }
    AddInfo(FString::Printf(
        TEXT(
            "Imported %d packages in %.0f s: %d levels with %d objects, %d textures, %d materials, %d material instances, %d static meshes, %d Blueprints, %d other assets (%d kept for reference only), %d parameter collections. %d not imported, %d warnings."),
        Result.Imported.Num(), FPlatformTime::Seconds() - Start, Levels, Result.LevelObjects, Textures, Materials,
        MaterialInstances, Meshes, Blueprints, Others, Result.ReferenceOnly.Num(), Collections,
        Result.NotImported.Num(), Result.Warnings.Num()));
    for (const FString& Problem : Result.Problems)
        AddInfo(TEXT("Not imported. ") + Problem);
    TArray<FString> FaithfulTypes;
    for (const auto& Pair : Result.Imported)
    {
        if (Pair.Value != TEXT("PhysicsAsset") && Pair.Value != TEXT("AnimMontage") &&
            Pair.Value != TEXT("GameSceneData"))
            continue;
        const FString Path = Pair.Key + TEXT(".") + FPackageName::GetShortName(Pair.Key);
        UObject* Asset = LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!TestNotNull(TEXT("Actual recovered object: ") + Path, Asset))
            continue;
        const FP4OriginalStatus Original = P4QueryOriginalAsset(Asset);
        TestTrue(TEXT("Faithful recovery retains original read-only status: ") + Path,
            Original.IsOriginal() && Original.Record.ReadOnly);
        TestFalse(TEXT("and the editor does not let it be edited: ") + Path, P4Kadan::GetAssetPolicy(Asset).AllowEdit);
        const FP4OriginalDiagnostic Compared = P4DiagnoseOriginalAsset(Asset);
        TestTrue(
            TEXT("Actual recovery cook equals original: ") + Path, Compared.Comparison == EP4OriginalComparison::Equal);
        FaithfulTypes.Add(Pair.Value + TEXT(" ") + Path + TEXT(" comparison=") +
            FString::FromInt(int32(Compared.Comparison)) + TEXT(" error=") + Compared.Error);
    }
    FaithfulTypes.Sort();
    const FString FaithfulFile = FPaths::ProjectSavedDir() / TEXT("P4MapKit/RecoveryImports/Cave000FaithfulTypes.txt");
    TestTrue(TEXT("Keep exact recovered physics/montage/scene comparison identities"),
        FFileHelper::SaveStringToFile(FString::Join(FaithfulTypes, TEXT("\n")) + TEXT("\n"), *FaithfulFile,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    AddInfo(FString::Printf(TEXT("Actual recovered physics/montage/scene comparisons: %d. Details: %s"),
        FaithfulTypes.Num(), *FaithfulFile));
    // Nothing is made up in place of a game asset. One Kadan cannot import for real stays the game's: the
    // game's own asset kept for reference only, read-only, or, when even that cannot be made, nothing in
    // the project and the Blueprint keeping the game's value. The report names each.
    FARFilter Filter;
    Filter.PackagePaths.Add(TEXT("/Game"));
    Filter.bRecursivePaths = true;
    Filter.TagsAndValues.Add(FName(TEXT("P4BlueprintStandIn")), TOptional<FString>(FString(TEXT("1"))));
    TArray<FAssetData> StandIns;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, StandIns);
    TArray<TSharedPtr<FP4KadanSourceAsset>> GameSources;
    TArray<FString> SourceProblems;
    TSet<FName> SourcePackages;
    if (!TestTrue(TEXT("Read game-present assets for the stand-in check"),
            P4KadanScanGame(Game, GameSources, SourceProblems)))
        return false;
    for (const auto& Source : GameSources)
        SourcePackages.Add(FName(*Source->Package));
    for (const FAssetData& One : StandIns)
        AddInfo(TEXT("Stand-in. ") + One.PackageName.ToString() + TEXT(" (") + One.AssetClass.ToString() + TEXT(")"));
    TestEqual(TEXT("Stand-ins"), StandIns.Num(), 0);
    AddInfo(FString::Printf(TEXT("%d stand-ins. %d skeletal meshes, %d skeletons, %d physics assets imported."),
        StandIns.Num(), Kinds.FindRef(TEXT("SkeletalMesh")), Kinds.FindRef(TEXT("Skeleton")),
        Kinds.FindRef(TEXT("PhysicsAsset"))));
    // The kinds a map import leaves the game's that the Blueprints of this map name.
    const TSet<FString> KeptKinds = {
        TEXT("DataTable"), TEXT("EftCustomRequest"), TEXT("EftRequest"), TEXT("ParticleSystem")};
    TMap<FString, int32> KeptCounts;
    int32 KeptForReference = 0, KeptWhole = 0;
    for (const auto& Pair : Result.Imported)
    {
        if (!KeptKinds.Contains(Pair.Value))
            continue;
        UObject* Asset = LoadObject<UObject>(
            nullptr, *(Pair.Key + TEXT(".") + FPackageName::GetShortName(Pair.Key)), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!TestNotNull(TEXT("The game's asset is in the project: ") + Pair.Key, Asset))
            continue;
        const P4Kadan::FAssetPolicy Policy = P4Kadan::GetAssetPolicy(Asset);
        if (!Result.ReferenceOnly.Contains(Pair.Key))
        {
            ++KeptWhole;
            ++KeptCounts.FindOrAdd(Pair.Value + TEXT(" whole"));
            continue;
        }
        ++KeptForReference;
        ++KeptCounts.FindOrAdd(Pair.Value + TEXT(" for reference"));
        TestTrue(TEXT("The game's asset is kept for reference only and is read-only: ") + Pair.Key,
            P4QueryOriginalAsset(Asset).IsOriginal() && Policy.ReferenceOnly && !Policy.AllowEdit && !Policy.StandIn);
        TestTrue(TEXT("and the report names it with the reason: ") + Pair.Key,
            Result.Warnings.ContainsByPredicate(
                [&](const FString& Warning)
                {
                    return Warning.StartsWith(Pair.Key + TEXT(" (")) &&
                        Warning.Contains(TEXT("kept for reference only"));
                }));
    }
    for (const FString& Reference : Result.KeptAsTheGames)
    {
        AddInfo(TEXT("Stays the game's with nothing in its place. ") + Reference);
        const FString Package = FPackageName::ObjectPathToPackageName(Reference);
        ++KeptCounts.FindOrAdd(TEXT("nothing in its place"));
        TestTrue(TEXT("The report names a game asset that stays the game's: ") + Reference,
            Result.Warnings.ContainsByPredicate([&](const FString& Warning)
                { return Warning.StartsWith(Reference) && Warning.Contains(TEXT("stays the game's")); }));
        TestFalse(TEXT("and nothing is made in its place: ") + Reference,
            FindObject<UObject>(nullptr, *Reference) || FPackageName::DoesPackageExist(Package));
    }
    TArray<FString> KeptLines;
    for (const auto& Pair : KeptCounts)
        KeptLines.Add(FString::Printf(TEXT("%s %d"), *Pair.Key, Pair.Value));
    KeptLines.Sort();
    AddInfo(FString::Printf(
        TEXT(
            "Game assets the Blueprints name of kinds a map import leaves the game's: %d imported whole, %d kept for reference only, %d with nothing in their place (%s)."),
        KeptWhole, KeptForReference, Result.KeptAsTheGames.Num(), *FString::Join(KeptLines, TEXT(", "))));
    // Cave000: 28 tables, 16 custom effect requests, one effect request and 10 particle systems imported
    // whole; 22 tables, 16 custom effect requests and 4 particle systems kept for reference only; none
    // that stay the game's with nothing in their place.
    TestEqual(TEXT("Game assets the Blueprints name, imported whole"), KeptWhole, 55);
    TestEqual(TEXT("Game assets the Blueprints name, kept for reference only"), KeptForReference, 42);
    TestEqual(TEXT("Game assets the Blueprints name that stay the game's with nothing in their place"),
        Result.KeptAsTheGames.Num(), 0);
    // Blueprints of this map name 11 objects the game does not ship. Their references stay empty, and
    // nothing is made in their place.
    int32 EmptyAndAbsent = 0;
    for (const FString& Reference : Result.EmptyReferences)
    {
        AddInfo(TEXT("Empty reference to an object the game does not ship. ") + Reference);
        const FString Package = FPackageName::ObjectPathToPackageName(Reference);
        if (!SourcePackages.Contains(FName(*Package)) && !FindObject<UObject>(nullptr, *Reference) &&
            !FPackageName::DoesPackageExist(Package))
            ++EmptyAndAbsent;
    }
    TestEqual(TEXT("Empty references to objects the game does not ship"), Result.EmptyReferences.Num(), 11);
    TestEqual(TEXT("Nothing is made in place of an object the game does not ship"), EmptyAndAbsent,
        Result.EmptyReferences.Num());
    TestEqual(
        TEXT("Blueprint identities acquired natively from original containers"), Result.OriginalIdentityPackages, 8);
    TestFalse(TEXT("The import was not cancelled"), Result.Cancelled);
    TestEqual(TEXT("Levels"), Levels, 13);
    TestEqual(TEXT("Objects in the levels"), Result.LevelObjects, 1648);
    TestEqual(TEXT("Textures"), Textures, 141);
    TestEqual(TEXT("Materials"), Materials, 61);
    TestEqual(TEXT("Material instances"), MaterialInstances, 163);
    TestEqual(TEXT("Static meshes"), Meshes, 190);
    TestEqual(TEXT("Blueprints"), Blueprints, 115);
    TestEqual(TEXT("Skeletal meshes"), Kinds.FindRef(TEXT("SkeletalMesh")), 16);
    TestEqual(TEXT("Skeletons"), Kinds.FindRef(TEXT("Skeleton")), 9);
    TestEqual(TEXT("Physics assets"), Kinds.FindRef(TEXT("PhysicsAsset")), 7);
    TestEqual(TEXT("Other assets the map needs"), Others, 1026);
    TestEqual(TEXT("Packages not imported"), Result.NotImported.Num(), 0);
    TestEqual(TEXT("Parameter collections"), Collections, 2);
    return !HasAnyErrors();
}
// A focused proof for the generated animation fields admitted by the native
// planner. It imports real skeletons through the same closure as a map import.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanAnimationRecoveryTest, "P4MapKit.Kadan.AnimationRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanAnimationRecoveryTest::RunTest(const FString&)
{
    using P4MapKit::Blueprint::J;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty())
    {
        AddError(TEXT("The animation recovery test needs P4_NATIVE_ROOT."));
        return false;
    }
    const FString Game = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    const TArray<FString> Names = {TEXT("/Game/Carrot4/Characters/Buddy/Animation/ABP_Happy"),
        TEXT("/Game/Carrot4/Characters/Player/ABP_Pikmin_EPIC"), TEXT("/Game/Carrot4/Characters/Teki/Baby/ABP_Baby"),
        TEXT("/Game/Carrot4/Objects/Gimmicks/CrackPotL/Animation/ABP_CrackPot"),
        TEXT("/Game/Carrot4/Objects/Items/Honey/Animation/ABP_Honey"),
        TEXT("/Game/Carrot4/Objects/Madori/DungeonExit/Animation/ABP_DungeonExit"),
        TEXT("/Game/Carrot4/Objects/Pod/ABP_Pod")};
    P4KadanImportTests::FMade Made(TEXT("KadanAnimationRecovery"));
    if (!TestTrue(TEXT("the test can keep its exact import cleanup list"), Made.Ready))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Game;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    if (Settings.Retoc.IsEmpty())
        Settings.Retoc = FPaths::GetPath(FPaths::GetPath(Root)) / TEXT("private/retoc/retoc.exe");
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Settings.Repak.IsEmpty() && !Settings.Retoc.IsEmpty())
        Settings.Repak = FPaths::GetPath(FPaths::GetPath(Settings.Retoc)) / TEXT("repak/repak.exe");
    const FP4KadanImportPlan Plan = P4KadanPlanImport(Settings, TSet<FString>(Names), false);
    for (const FString& Problem : Plan.Problems)
        AddError(Problem);
    if (!TestTrue(TEXT("Read original animation closure from actual source inputs"), Plan.Problems.Num() == 0))
        return false;
    const auto& Scanned = Plan.Scanned;
    const FP4KadanImportResult Imported = P4KadanRunImport(Settings, Plan, false);
    if (!KeepImportTestReport(*this, TEXT("AnimationRecovery"), Imported))
        return false;
    for (const FString& Problem : Imported.Problems)
        AddInfo(TEXT("Not imported. ") + Problem);
    int32 Checked = 0, LayeredNodes = 0;
    for (const FString& Name : Names)
    {
        if (!TestTrue(TEXT("Animation imported: ") + Name, Imported.Imported.Contains(Name)))
            continue;
        const TSharedPtr<FP4KadanSourceAsset>* Source = Scanned.FindByPredicate(
            [&](const TSharedPtr<FP4KadanSourceAsset>& Asset) { return Asset->Package == Name; });
        UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(
            nullptr, *(Name + TEXT(".") + FPackageName::GetShortName(Name)), nullptr, LOAD_NoWarn | LOAD_Quiet);
        UAnimBlueprintGeneratedClass* Class = Blueprint ? Cast<UAnimBlueprintGeneratedClass>(Blueprint->GeneratedClass)
                                                        : nullptr;
        if (!TestTrue(TEXT("Animation compiled: ") + Name, Source && Class && Blueprint->Status != BS_Error))
            continue;
        TArray<uint8> Header, Body;
        if (!TestTrue(TEXT("Read animation source: ") + Name,
                FFileHelper::LoadFileToArray(Header, *(*Source)->Header) &&
                    FFileHelper::LoadFileToArray(Body, *(*Source)->Body)))
            continue;
        std::vector<std::uint8_t> SourceHeader(Header.GetData(), Header.GetData() + Header.Num()),
            SourceBody(Body.GetData(), Body.GetData() + Body.Num());
        auto Package = P4MapKit::ReadPackage(SourceHeader, SourceBody);
        Package.Package = TCHAR_TO_UTF8(*Name);
        const J Document = P4MapKit::Blueprint::ReadBlueprintDocument(Package, SourceBody);
        const J& Values = Document["class"]["values"];
        const auto Text = [](const J& Value) { return FString(UTF8_TO_TCHAR(Value.S().c_str())); };
        TestTrue(TEXT("Target skeleton identity: ") + Name,
            Class->TargetSkeleton && Class->TargetSkeleton->GetPathName() == Text(Values["TargetSkeleton"]));
        TestTrue(TEXT("Real skeleton has bones: ") + Name,
            Class->TargetSkeleton && Class->TargetSkeleton->GetReferenceSkeleton().GetRawBoneNum() > 0);
        const J& Saved = Values["OrderedSavedPoseIndicesMap"]["map"];
        TestEqual(TEXT("Saved-pose graph count: ") + Name, Class->OrderedSavedPoseIndicesMap.Num(),
            int32(Saved.Items.size()));
        for (const J& Pair : Saved.Items)
        {
            const FCachedPoseIndices* Indices = Class->OrderedSavedPoseIndicesMap.Find(
                FName(*Text(Pair[std::size_t(0)])));
            const auto& Expected = Pair[1]["OrderedSavedPoseNodeIndices"].Items;
            TestTrue(TEXT("Saved-pose graph present: ") + Name, Indices != nullptr);
            if (!Indices)
                continue;
            TestEqual(TEXT("Saved-pose index count: ") + Name, Indices->OrderedSavedPoseNodeIndices.Num(),
                int32(Expected.size()));
            for (int32 Index = 0; Index < Indices->OrderedSavedPoseNodeIndices.Num() && Index < int32(Expected.size());
                ++Index)
                TestEqual(TEXT("Saved-pose compiler index: ") + Name, Indices->OrderedSavedPoseNodeIndices[Index],
                    int32(Expected[std::size_t(Index)].I()));
        }
        const J* Defaults = nullptr;
        for (const J& Object : Document["objects"].Items)
            if (Object["name"].Dump() == Document["class"]["class_default_object"].Dump())
                Defaults = &Object["values"];
        if (!Defaults)
        {
            AddError(TEXT("Animation source has no default object: ") + Name);
            continue;
        }
        UObject* Default = Class->GetDefaultObject();
        for (FStructProperty* Node : Class->GetAnimNodeProperties())
        {
            if (!Node->Struct->GetName().Equals(TEXT("AnimNode_LayeredBoneBlend")))
                continue;
            const std::string NodeName(TCHAR_TO_UTF8(*Node->GetName()));
            if (!Defaults->Has(NodeName))
            {
                AddError(TEXT("Compiled layered node has no source: ") + Node->GetName());
                continue;
            }
            const J& Original = (*Defaults)[NodeName];
            FStructProperty* GuidProperty = FindFProperty<FStructProperty>(Node->Struct, TEXT("SkeletonGuid"));
            FArrayProperty* WeightsProperty = FindFProperty<FArrayProperty>(Node->Struct, TEXT("PerBoneBlendWeights"));
            if (!TestTrue(TEXT("Layered node exposes compiled cache fields"), GuidProperty && WeightsProperty))
                continue;
            void* NodeValue = Node->ContainerPtrToValuePtr<void>(Default);
            const FGuid* Guid = GuidProperty->ContainerPtrToValuePtr<FGuid>(NodeValue);
            const auto& ExpectedGuid = Original["SkeletonGuid"].Items;
            const FGuid SourceGuid(uint32(ExpectedGuid[0].U()), uint32(ExpectedGuid[1].U()),
                uint32(ExpectedGuid[2].U()), uint32(ExpectedGuid[3].U()));
            TestTrue(TEXT("Layered node skeleton GUID matches source: ") + Node->GetName(), *Guid == SourceGuid);
            FScriptArrayHelper Weights(WeightsProperty, WeightsProperty->ContainerPtrToValuePtr<void>(NodeValue));
            const auto& ExpectedWeights = Original["PerBoneBlendWeights"].Items;
            TestEqual(
                TEXT("Layered bone-weight count: ") + Node->GetName(), Weights.Num(), int32(ExpectedWeights.size()));
            FStructProperty* WeightStruct = CastField<FStructProperty>(WeightsProperty->Inner);
            FIntProperty* SourceIndex = WeightStruct
                ? FindFProperty<FIntProperty>(WeightStruct->Struct, TEXT("SourceIndex"))
                : nullptr;
            FFloatProperty* BlendWeight = WeightStruct
                ? FindFProperty<FFloatProperty>(WeightStruct->Struct, TEXT("BlendWeight"))
                : nullptr;
            if (!TestTrue(TEXT("Layered bone weights expose index and weight"), SourceIndex && BlendWeight))
                continue;
            for (int32 Index = 0; Index < Weights.Num() && Index < int32(ExpectedWeights.size()); ++Index)
            {
                const J& Expected = ExpectedWeights[std::size_t(Index)];
                TestEqual(TEXT("Layered bone source index"),
                    SourceIndex->GetPropertyValue_InContainer(Weights.GetRawPtr(Index)),
                    int32(Expected["SourceIndex"].I()));
                TestTrue(TEXT("Layered bone blend weight"),
                    FMath::IsNearlyEqual(BlendWeight->GetPropertyValue_InContainer(Weights.GetRawPtr(Index)),
                        float(Expected["BlendWeight"].D()), 0.00001f));
            }
            ++LayeredNodes;
        }
        ++Checked;
    }
    AddInfo(FString::Printf(
        TEXT(
            "Checked %d animation Blueprints and %d layered bone caches against original values; %d packages imported, %d not imported."),
        Checked, LayeredNodes, Imported.Imported.Num(), Imported.NotImported.Num()));
    TestEqual(TEXT("All animation Blueprints checked"), Checked, 7);
    TestEqual(TEXT("Happy and Pikmin layered bone caches checked"), LayeredNodes, 7);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanFoliageRecoveryTest, "P4MapKit.Kadan.FoliageRecoveryCave000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanFoliageRecoveryTest::RunTest(const FString&)
{
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty())
    {
        AddError(TEXT("Foliage recovery requires actual Cave000 source inputs."));
        return false;
    }
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    if (Settings.Retoc.IsEmpty())
        Settings.Retoc = FPaths::GetPath(FPaths::GetPath(Root)) / TEXT("private/retoc/retoc.exe");
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Settings.Repak.IsEmpty() && !Settings.Retoc.IsEmpty())
        Settings.Repak = FPaths::GetPath(FPaths::GetPath(Settings.Retoc)) / TEXT("repak/repak.exe");
    P4KadanImportTests::FMade Made(TEXT("KadanFoliageRecovery"));
    if (!TestTrue(TEXT("Exact fixture retirement is ready"), Made.Ready))
        return false;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    if (!TestTrue(TEXT("Original Cave000 sources are read"), P4KadanScanGame(Settings.GameFolder, Sources, Problems)))
        return false;
    TSet<FString> Roots;
    for (const auto& Source : Sources)
        if (Source->Kind == TEXT("FoliageType_InstancedStaticMesh") && Source->Package.Contains(TEXT("/Terrain/Cave/")))
            Roots.Add(Source->Package);
    if (!TestTrue(TEXT("All fifteen previously reference-only foliage sources are available"), Roots.Num() >= 15))
        return false;
    const FP4KadanImportPlan Plan = P4KadanPlanImport(Settings, Roots, false);
    const FP4KadanImportResult Imported = P4KadanRunImport(Settings, Plan, false);
    if (!KeepImportTestReport(*this, TEXT("FoliageRecoveryCave000"), Imported))
        return false;
    for (const FString& Problem : Imported.Problems)
        AddInfo(Problem);
    int32 Equal = 0;
    for (const FString& Name : Roots)
    {
        const FString Path = Name + TEXT(".") + FPackageName::GetShortName(Name);
        UObject* Asset = LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!TestTrue(TEXT("Original foliage has valid derived native storage: ") + Name,
                Imported.Imported.Contains(Name) && Asset && Asset->GetClass() == P4RecoveredFoliageClass() &&
                    Cast<UFoliageType_InstancedStaticMesh>(Asset)))
            continue;
        const FP4OriginalStatus Status = P4QueryOriginalAsset(Asset);
        TestTrue(TEXT("Recovered game foliage remains read-only"), Status.IsOriginal() && Status.Record.ReadOnly);
        FP4ModDescriptor Mod;
        Mod.Id = TEXT("test.foliagerecovery");
        Mod.Name = TEXT("Foliage recovery");
        Mod.Version = TEXT("1.0.0");
        TestFalse(TEXT("Recovered game foliage cannot ship as creator content"),
            P4ExportAssetAvailability(Asset, Mod).IsAvailable());
        TestFalse(TEXT("All game fields and native bytes are held"),
            Status.Record.ReferenceOnly || Imported.ReferenceOnly.Contains(Name));
        const FP4OriginalDiagnostic Compared = P4DiagnoseOriginalAsset(Asset);
        if (TestTrue(TEXT("Actual translated cook equals original foliage: ") + Name,
                Compared.Comparison == EP4OriginalComparison::Equal))
            ++Equal;
        else
            AddInfo(Compared.Error);
    }
    TestEqual(TEXT("Every original foliage is recovered and cook-compared"), Equal, Roots.Num());
    return !HasAnyErrors();
}
#endif

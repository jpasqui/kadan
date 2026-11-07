#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "P4KadanImport.h"
#include "P4KadanImportSources.h"
#include "P4KadanBlueprintPlans.h"
#include "P4BlueprintImport.h"
#include "P4OriginalClassComparison.h"
#include "P4NativeHash.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/ActorComponent.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
// Test-only namespace custody. Moves the small owned fixture package, never
// copies game payloads and never changes a user's project. Fresh native import
// must create the original canonical package through its ordinary pipeline.
struct FFreshRoomNamespace
{
    FString Package, File, Backup, Error;
    TArray<TPair<FString, FString>> Moved;
    bool Prepared = false, Opened = false, Restored = false;
    bool Unload()
    {
        if (UPackage* Loaded = FindPackage(nullptr, *Package))
        {
            Loaded->SetDirtyFlag(false);
            FText Why;
            if (!UPackageTools::UnloadPackages({Loaded}, Why))
            {
                Error = Why.ToString();
                return false;
            }
        }
        CollectGarbage(RF_NoFlags);
        return true;
    }
    bool Prepare(const FString& Name, const FString& NativeRoot, const FString& Folder)
    {
        Package = Name;
        File = FPaths::ConvertRelativePathToFull(
            FPackageName::LongPackageNameToFilename(Name, FPackageName::GetAssetPackageExtension()));
        Backup = FPaths::ConvertRelativePathToFull(Folder / TEXT("prior-room"));
        auto Normalize = [](FString Path)
        {
            FPaths::NormalizeFilename(Path);
            Path = FPaths::ConvertRelativePathToFull(Path);
            FPaths::CollapseRelativeDirectories(Path);
            FPaths::NormalizeDirectoryName(Path);
            return Path;
        };
        // Normalize Windows separators before GetPath: its directory split is
        // slash based, whereas P4_NATIVE_ROOT is normally a Windows Path string.
        const FString Root = NativeRoot.IsEmpty() ? FString() : Normalize(NativeRoot);
        const FString Repository = FPaths::GetPath(FPaths::GetPath(Root));
        const FString Project = Normalize(FPaths::ProjectDir());
        const FString OwnedProject = Normalize(Repository / TEXT("private/game-references-validation/project"));
        const FString OwnedRoot = Normalize(Repository / TEXT("private/mapkit-native-workflow"));
        const FString Content = Normalize(FPaths::ProjectContentDir()), Saved = Normalize(FPaths::ProjectSavedDir());
        File = Normalize(File);
        Backup = Normalize(Backup);
        const bool RootMatches = !Root.IsEmpty() && FPaths::IsSamePath(Root, OwnedRoot);
        const bool ProjectMatches = FPaths::IsSamePath(Project, OwnedProject);
        const bool FileWithin = FPaths::IsUnderDirectory(File, Content),
                   BackupWithin = FPaths::IsUnderDirectory(Backup, Saved);
        if (!RootMatches || !ProjectMatches || !FileWithin || !BackupWithin)
        {
            Error = FString::Printf(
                TEXT(
                    "Fresh Blueprint namespace proof is restricted to the owned native fixture project. root=[%s] expected-root=[%s] project=[%s] expected-project=[%s] content=[%s] file=[%s] saved=[%s] custody=[%s] guards=%d/%d/%d/%d"),
                *Root, *OwnedRoot, *Project, *OwnedProject, *Content, *File, *Saved, *Backup, int32(RootMatches),
                int32(ProjectMatches), int32(FileWithin), int32(BackupWithin));
            return false;
        }
        if (UPackage* Loaded = FindPackage(nullptr, *Package))
            if (Loaded->IsDirty())
            {
                Error = TEXT("Owned existing fixture has unsaved changes");
                return false;
            }
        if (!Unload() || !IFileManager::Get().MakeDirectory(*Backup, true))
            return false;
        // Preflight every destination before moving any original file.
        for (const TCHAR* Extension : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
        {
            const FString From = FPaths::ChangeExtension(File, Extension),
                          Into = Backup / FPaths::GetCleanFilename(From);
            if (IFileManager::Get().FileExists(*Into))
            {
                Error = TEXT("Previous owned namespace custody has not been restored: ") + Into;
                return false;
            }
        }
        Prepared = true;
        for (const TCHAR* Extension : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
        {
            const FString From = FPaths::ChangeExtension(File, Extension),
                          Into = Backup / FPaths::GetCleanFilename(From);
            if (IFileManager::Get().FileExists(*From))
            {
                if (!IFileManager::Get().Move(*Into, *From, false, true, false, true))
                {
                    Error = TEXT("Cannot retain owned fixture package: ") + From;
                    return false;
                }
                Moved.Emplace(From, Into);
            }
        }
        Opened = true;
        return true;
    }
    bool Restore()
    {
        if (!Prepared || Restored)
            return true;
        if (!Unload())
            return false;
        if (Opened)
            for (const TCHAR* Extension : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
            {
                const FString New = FPaths::ChangeExtension(File, Extension);
                if (IFileManager::Get().FileExists(*New) && !IFileManager::Get().Delete(*New, false, true))
                {
                    Error = TEXT("Cannot remove owned fresh proof output: ") + New;
                    return false;
                }
            }
        Opened = false;
        for (int32 N = Moved.Num() - 1; N >= 0; --N)
        {
            const auto& Pair = Moved[N];
            if (!IFileManager::Get().Move(*Pair.Key, *Pair.Value, false, true, false, true))
            {
                Error = TEXT("Cannot restore owned fixture package: ") + Pair.Key;
                return false;
            }
            Moved.RemoveAt(N);
        }
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
            .Get()
            .ScanFilesSynchronous({File}, true);
        Restored = true;
        return true;
    }
    ~FFreshRoomNamespace()
    {
        if (!Restore())
            UE_LOG(LogTemp, Error, TEXT("Fresh room namespace restore failed: %s"), *Error);
    }
};
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4FreshRoomBlueprint, "P4MapKit.NativeImport.FreshRoomBlueprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4FreshRoomBlueprint::RunTest(const FString&)
{
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_FRESH_ROOM_SOURCE"));
    if (Header.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Opt-in fresh native room proof: set P4_NATIVE_FRESH_ROOM_SOURCE to retained CMA_T02_RM_2x2_002.uasset"));
        return true;
    }
    const FString ConfiguredRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString NativeRoot = ConfiguredRoot.IsEmpty() ? FString() : FPaths::ConvertRelativePathToFull(ConfiguredRoot);
    const FString Package = TEXT("/Game/Carrot4/Terrain/Cave/Type/Soil/BlueprintActor/CMA_T02_RM_2x2_002");
    const FString AssetPath = Package + TEXT(".CMA_T02_RM_2x2_002");
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/FreshRoomProof");
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    FString Error;
    const FString SourceRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_FRESH_ROOM_SOURCE_ROOT"));
    if (!SourceRoot.IsEmpty() &&
        !TestTrue(TEXT("Explicit retained source registry reads without payload conversion"),
            P4KadanScanGame(SourceRoot, Sources, Problems) && Problems.Num() == 0))
        return false;
    auto Room = MakeShared<FP4KadanSourceAsset>();
    Room->Package = Package;
    Room->Kind = TEXT("Blueprint");
    Room->Header = Header;
    Room->Body = FPaths::ChangeExtension(Header, TEXT("uexp"));
    Sources.RemoveAll(
        [&](const TSharedPtr<FP4KadanSourceAsset>& Row) { return Row.IsValid() && Row->Package == Package; });
    Sources.Add(Room);
    FP4KadanImportSourceScope SourceScope(Sources);
    FP4KadanSourceBinding Bound;
    if (!TestTrue(
            TEXT("Actual original room H/B source is bound and hashed"), P4KadanReadBoundSource(Package, Bound, Error)))
    {
        AddError(Error);
        return false;
    }
    FFreshRoomNamespace Namespace;
    if (!TestTrue(TEXT("Only the owned fixture namespace is staged for truly fresh declaration"),
            Namespace.Prepare(Package, NativeRoot, Folder)))
    {
        AddError(Namespace.Error);
        return false;
    }
    auto Finish = [&](bool Good)
    {
        if (!TestTrue(TEXT("Original owned fixture namespace is restored after fresh proof"), Namespace.Restore()))
        {
            AddError(Namespace.Error);
            return false;
        }
        return Good && !HasAnyErrors();
    };
    FP4KadanBlueprintPlans Plans;
    FP4KadanImportSettings Settings;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    FP4CurrentGameRegistryPtr Reader;
    if (!TestTrue(TEXT("Normal native planner builds the fresh actual room from existing source files"),
            P4KadanPlanBlueprints(Sources, {Package}, Folder / TEXT("plans"), Plans, Error, Settings, Reader)))
    {
        AddError(Error);
        return Finish(false);
    }
    if (!TestTrue(TEXT("Actual room plan is admitted without reconstruction omissions"),
            Plans.Planned.Contains(Package) && !Plans.Refused.Contains(Package)))
    {
        for (const auto& Pair : Plans.Refused)
            AddInfo(Pair.Key + TEXT(": ") + Pair.Value);
        return Finish(false);
    }
    const auto Result = UP4BlueprintImport::RunSelectedNative(
        Plans.Folder, Folder / TEXT("import-result.json"), {Package}, false, {}, true);
    if (!TestTrue(TEXT("Ordinary fresh native Declare/Values/Compile/Save completes"),
            Result.bOk && Result.CompletedPackages.Contains(FName(*Package))))
    {
        for (const auto& Why : Result.Errors)
            AddError(Why);
        return Finish(false);
    }
    FString Text;
    TSharedPtr<FJsonObject> Plan;
    if (!TestTrue(TEXT("Own native plan is readable"),
            FFileHelper::LoadFileToString(Text, *(Plans.Folder / TEXT("import") / Package.Mid(1) + TEXT(".json"))) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Plan) && Plan.IsValid()))
        return Finish(false);
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    auto Check = [&](const TCHAR* Stage)
    {
        if (!TestNotNull(TEXT("Genuine fresh imported Blueprint exists"), Blueprint) ||
            !Blueprint->SimpleConstructionScript)
            return false;
        int32 Compared = 0;
        for (const auto& Item : Plan->GetArrayField(TEXT("components")))
        {
            const auto Component = Item->AsObject();
            if (Component->GetStringField(TEXT("cls")) != TEXT("/Script/Carrot4.StaticCollisionComponent"))
                continue;
            for (const auto& Value : Component->GetArrayField(TEXT("values")))
            {
                const auto Source = Value->AsObject();
                if (Source->GetStringField(TEXT("name")) != TEXT("C4TerrainTagContainer"))
                    continue;
                const FString Variable = Component->GetStringField(TEXT("variable"));
                const FString ExpectedTemplate = Variable + TEXT("_GEN_VARIABLE");
                USCS_Node* Node = Blueprint->SimpleConstructionScript->FindSCSNode(FName(*Variable));
                UActorComponent* Target = Node ? Node->ComponentTemplate : nullptr;
                auto* Field = Target ? FindFProperty<FStructProperty>(Target->GetClass(), TEXT("C4TerrainTagContainer"))
                                     : nullptr;
                const bool Identity = Target && Field && Target->GetName() == ExpectedTemplate &&
                    Target->GetClass()->GetPathName() == Component->GetStringField(TEXT("cls")) &&
                    Field->GetOwnerStruct() == Target->GetClass() &&
                    Field->Struct->GetPathName() == TEXT("/Script/Engine.C4TerrainTagContainer");
                AddInfo(FString::Printf(
                    TEXT(
                        "Fresh terrain %s variable=%s expected-template=%s actual-template=%s class=%s field-owner=%s struct=%s"),
                    Stage, *Variable, *ExpectedTemplate, Target ? *Target->GetName() : TEXT("absent"),
                    Target ? *Target->GetClass()->GetPathName() : TEXT("absent"),
                    Field && Field->GetOwnerStruct() ? *Field->GetOwnerStruct()->GetPathName() : TEXT("absent"),
                    Field ? *Field->Struct->GetPathName() : TEXT("absent")));
                if (!TestTrue(
                        TEXT(
                            "Actual source SCS variable maps to its ordinary generated template and original native field"),
                        Identity))
                    return false;
                FStructOnScope Scratch(Field->Struct);
                const FString Expected = Source->GetStringField(TEXT("text"));
                const TCHAR* End = Field->ImportText(*Expected, Scratch.GetStructMemory(), PPF_None, Target);
                const bool Equal = End && !*End &&
                    Field->Identical(Field->ContainerPtrToValuePtr<void>(Target), Scratch.GetStructMemory(), PPF_None);
                if (!Equal)
                {
                    FString Actual;
                    Field->ExportTextItem(
                        Actual, Field->ContainerPtrToValuePtr<void>(Target), nullptr, Target, PPF_None, nullptr);
                    AddInfo(TEXT("Fresh terrain expected=") + Expected + TEXT(" actual=") + Actual +
                        TEXT(" unconsumed=") + (End ? FString(End) : TEXT("import-refused")));
                }
                if (!TestTrue(TEXT("Complete bound source terrain values match typed fresh template storage"), Equal))
                    return false;
                ++Compared;
            }
        }
        if (!TestEqual(TEXT("All four actual source terrain component fields were compared"), Compared, 4))
            return false;
        TSharedPtr<FJsonObject> Record;
        FJsonSerializer::Deserialize(
            TJsonReaderFactory<TCHAR>::Create(
                Blueprint->GetOutermost()->GetMetaData()->GetValue(Blueprint, TEXT("P4OriginalRecord"))),
            Record);
        if (!TestTrue(TEXT("Fresh ordinary import has no unsupported terrain value notes"), Record.IsValid()))
            return false;
        for (const auto& Note : Record->GetArrayField(TEXT("values")))
            if (Note->AsObject()->GetStringField(TEXT("property")) == TEXT("C4TerrainTagContainer"))
            {
                AddError(TEXT("Fresh import retained an unsupported terrain field"));
                return false;
            }
        const auto Comparison = P4CompareOriginalClassNative(Bound.HeaderPath, Bound.BodyPath);
        IFileManager::Get().MakeDirectory(*Folder, true);
        FFileHelper::SaveStringToFile(P4OriginalClassComparisonJson(Comparison),
            *(Folder / FString(Stage) + TEXT(".json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        if (!TestTrue(FString(TEXT("Fresh complete room class equals actual source ")) + Stage,
                Comparison.IsValid() && Comparison.Same && Comparison.ValuesKnown && Comparison.ValuesNotHeld == 0))
        {
            for (const auto& Line : Comparison.OnlyGameSays)
                AddInfo(TEXT("Only original: ") + Line);
            for (const auto& Line : Comparison.OnlyProjectSays)
                AddInfo(TEXT("Only fresh: ") + Line);
            return false;
        }
        return TestTrue(TEXT("Original source snapshot stays unchanged"), P4KadanSourceBindingCurrent(Bound, Error));
    };
    if (!Check(TEXT("after-fresh-import")))
        return Finish(false);
    UPackage* Loaded = Blueprint->GetOutermost();
    Blueprint = nullptr;
    FText UnloadError;
    if (!TestTrue(TEXT("Fresh saved Blueprint unloads for a cold source proof"),
            UPackageTools::UnloadPackages({Loaded}, UnloadError)))
    {
        AddError(UnloadError.ToString());
        return Finish(false);
    }
    Loaded = nullptr;
    CollectGarbage(RF_NoFlags);
    Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    return Finish(Check(TEXT("after-cold-reload")));
}
#endif

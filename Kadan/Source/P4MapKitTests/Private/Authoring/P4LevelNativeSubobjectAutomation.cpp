#include "CoreMinimal.h"
#include "P4KadanTestSeams.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "P4LevelContent.h"
#include "P4LevelImportWorld.h"
#include "P4BoundedSourceRead.h"
#include "P4RegistryInputLease.h"
#include "P4BlueprintImport.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/package_reader.h"
THIRD_PARTY_INCLUDES_END
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
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
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace P4LevelNativeSubobjectTest
{
FString Full(FString Path)
{
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Path);
    FPaths::CollapseRelativeDirectories(Path);
    FPaths::NormalizeDirectoryName(Path);
    return Path;
}
// Class source registration deliberately accepts .uasset classes only. This
// gate consumes an actual World .umap pair without changing that contract.
struct FMapPair
{
    FString HeaderPath, BodyPath, SourceDigest;
    TArray<uint8> Header, Body;
    P4RegistryLease::FInput HeaderLease, BodyLease;
    bool Open(const FString& Path, FString& Error)
    {
        HeaderPath = Full(Path);
        BodyPath = FPaths::ChangeExtension(HeaderPath, TEXT("uexp"));
        if (Path.IsEmpty() || !FPaths::GetExtension(HeaderPath).Equals(TEXT("umap"), ESearchCase::CaseSensitive))
        {
            Error = TEXT("Actual map proof requires an explicit .umap header");
            return false;
        }
#if PLATFORM_WINDOWS
        if (!HeaderLease.Open(std::wstring(*HeaderPath)) || !BodyLease.Open(std::wstring(*BodyPath)))
        {
            Error = TEXT("Cannot hold actual map pair immutable");
            return false;
        }
#else
        Error = TEXT("Actual map immutable source custody unavailable");
        return false;
#endif
        return P4ReadBoundedSourceFile(HeaderPath, 64 * 1024 * 1024, Header, Error) &&
            P4ReadBoundedSourceFile(BodyPath, 64 * 1024 * 1024, Body, Error) &&
            uint64(Header.Num()) == HeaderLease.Size() && uint64(Body.Num()) == BodyLease.Size() &&
            P4HashSourceBuffers(Header.GetData(), Header.Num(), Body.GetData(), Body.Num(), SourceDigest) &&
            HeaderLease.Current() && BodyLease.Current();
    }
    bool Current(FString& Error) const
    {
        TArray<uint8> H, B;
        FString Digest;
        return HeaderLease.Current() && BodyLease.Current() &&
            P4ReadBoundedSourceFile(HeaderPath, 64 * 1024 * 1024, H, Error) &&
            P4ReadBoundedSourceFile(BodyPath, 64 * 1024 * 1024, B, Error) &&
            P4HashSourceBuffers(H.GetData(), H.Num(), B.GetData(), B.Num(), Digest) &&
            P4ExactNativeString(Digest, SourceDigest) && HeaderLease.Current() && BodyLease.Current();
    }
};
struct FProof
{
    FAutomationTestBase& Test;
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    explicit FProof(FAutomationTestBase& In) : Test(In)
    {
        Json->SetBoolField(TEXT("passed"), false);
        Json->SetStringField(TEXT("stage"), TEXT("preflight"));
    }
    ~FProof()
    {
        bool Passed = false;
        Json->TryGetBoolField(TEXT("passed"), Passed);
        Json->SetBoolField(TEXT("passed"), Passed && !Test.HasAnyErrors());
        FString Text;
        const FString Out = FPaths::ProjectSavedDir() / TEXT("P4MapKit/LevelNativeSubobject/actual-source.json");
        if (!FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text)) ||
            !IFileManager::Get().MakeDirectory(*FPaths::GetPath(Out), true) ||
            !FFileHelper::SaveStringToFile(Text, *Out, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            Test.AddError(TEXT("Cannot retain native level subobject proof"));
    }
};
} //namespace P4LevelNativeSubobjectTest
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualLevelNativeSubobject, "P4MapKit.Kadan.ActualLevelNativeSubobjectIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualLevelNativeSubobject::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_LEVEL_SUBOBJECT")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Skipped actual native level subobject identity; requires P4_NATIVE_LEVEL_SUBOBJECT=1 and explicit source header"));
        return true;
    }
    using namespace P4LevelNativeSubobjectTest;
    const FString Native = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"))),
                  Repo = FPaths::GetPath(FPaths::GetPath(Native));
    if (!TestTrue(TEXT("Only the existing owned postprocess successor fixture is used"),
            !Native.IsEmpty() &&
                FPaths::IsSamePath(Full(FPaths::ProjectDir()), Repo / TEXT("private/native-postprocess-env/project"))))
        return false;
    FProof Receipt(*this);
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_LEVEL_SUBOBJECT_HEADER"));
    const FString Package = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");
    const FString EnvPackage = TEXT("/Game/Carrot4/Core/Graphics/System/BP_EnvController");
    if (!TestTrue(TEXT("Actual original map header is explicitly provided"), !Header.IsEmpty()))
        return false;
    FMapPair Source;
    FString Error;
    if (!TestTrue(TEXT("Actual World pair is bounded and held immutable"), Source.Open(Header, Error)))
    {
        AddError(Error);
        return false;
    }
    const std::vector<uint8> H(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num()),
        B(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num());
    const auto Inventory = P4MapKit::ReadPackage(H, B);
    if (!TestTrue(TEXT("Actual source package inventory is valid"),
            Inventory.Error.Code.empty() && FString(UTF8_TO_TCHAR(Inventory.Package.c_str())) == Package))
        return false;
    FString AssetFile;
    if (!TestTrue(TEXT("Existing Env must already be saved; no importer or planner is called"),
            FPackageName::DoesPackageExist(EnvPackage, nullptr, &AssetFile)))
        return false;
    FString AssetSha;
    int64 AssetBytes = 0;
    if (!TestTrue(TEXT("Existing Env saved bytes are pinned before loading"),
            P4HashFile(AssetFile, 16 * 1024 * 1024, AssetBytes, AssetSha)))
        return false;
    UBlueprint* BP = LoadObject<UBlueprint>(
        nullptr, *(EnvPackage + TEXT(".BP_EnvController")), nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!TestTrue(TEXT("Existing complete real Env class is required"),
            BP && BP->GeneratedClass && !P4BlueprintReconstructionIncomplete(BP)))
        return false;
    FP4OriginalRecord Record;
    if (!TestTrue(TEXT("Existing recovered original marker is preserved, not manufactured"),
            P4ReadOriginalRecord(BP, Record, Error) && Record.Package == EnvPackage &&
                Record.Kind == TEXT("blueprint") && Record.Fidelity == TEXT("recovered") &&
                Record.SourceDigestKind == TEXT("blueprint-import")))
        return false;
    const bool Dirty = BP->GetOutermost()->IsDirty();
    const FString OriginalRecord = BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4OriginalRecord"));
    ON_SCOPE_EXIT
    {
        FString Now;
        int64 N = 0;
        TestTrue(TEXT("Actual source and saved Env stay exact"),
            Source.Current(Error) && P4HashFile(AssetFile, 16 * 1024 * 1024, N, Now) && N == AssetBytes &&
                P4ExactNativeString(Now, AssetSha));
        TestTrue(TEXT("Existing Env record and dirty state unchanged"),
            BP->GetOutermost()->IsDirty() == Dirty &&
                P4ExactNativeString(
                    OriginalRecord, BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4OriginalRecord"))));
    };
    TStrongObjectPtr<UWorld> World(P4CreateOriginalImportWorld(
        MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("LevelNativeSubobject")),
        GetTransientPackage()));
    if (!TestNotNull(TEXT("Independent transient construction world"), World.Get()))
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
    if (!TestNotNull(TEXT("Ordinary native Env constructor"), Actor))
        return false;
    Actor->FinishSpawning(FTransform::Identity);
    const auto Proof = Receipt.Json;
    TArray<TSharedPtr<FJsonValue>> Rows;
    int32 Count = 0;
    for (size_t I = 0; I < Inventory.Exports.size(); ++I)
    {
        const auto& Export = Inventory.Exports[I];
        if (Export.Name != "StaticSunLight" && Export.Name != "SunLight" && Export.Name != "PostProcess")
            continue;
        std::string Path, OuterPath, TemplatePath, ClassPath;
        if (!Inventory.Reference(int32(I) + 1, Path) || !Inventory.Reference(Export.Outer, OuterPath) ||
            OuterPath != "Cave000_F00_P.PersistentLevel.BP_EnvController_C_0")
            continue;
        if (!TestTrue(TEXT("Source declares exact native default child class and explicit CDO template"),
                Inventory.Reference(Export.Template, TemplatePath) && Inventory.Reference(Export.Class, ClassPath) &&
                    TemplatePath ==
                        std::string(TCHAR_TO_UTF8(*(EnvPackage + TEXT(".Default__BP_EnvController_C.")))) +
                            Export.Name &&
                    ClassPath ==
                        (Export.Name == "PostProcess" ? "/Script/Engine.PostProcessComponent"
                                                      : "/Script/Engine.DirectionalLightComponent") &&
                    (Export.Flags & uint32(RF_DefaultSubObject))))
            return false;
        const FString Name = UTF8_TO_TCHAR(Export.Name.c_str());
        UObject* Child = StaticFindObjectFast(nullptr, Actor, FName(*Name));
        UObject* Template = StaticFindObjectFast(nullptr, BP->GeneratedClass->GetDefaultObject(), FName(*Name));
        UClass* Declared = FindObject<UClass>(nullptr, UTF8_TO_TCHAR(ClassPath.c_str()));
        if (!TestTrue(TEXT("Actual constructor child uses genuine qualified native storage"),
                Child && Template && Declared && Child->GetClass() != Declared &&
                    P4OriginalLevelStorageSubobjectMatchesForTest(
                        Child, Actor, Declared, Name, Export.Flags, Template)))
            return false;
        if (!TestFalse(TEXT("Missing source default-subobject authority refuses"),
                P4OriginalLevelStorageSubobjectMatchesForTest(
                    Child, Actor, Declared, Name, Export.Flags & ~uint32(RF_DefaultSubObject), Template)) ||
            !TestFalse(TEXT("Wrong source name refuses"),
                P4OriginalLevelStorageSubobjectMatchesForTest(
                    Child, Actor, Declared, Name + TEXT("_wrong"), Export.Flags, Template)) ||
            !TestFalse(TEXT("Wrong outer refuses"),
                P4OriginalLevelStorageSubobjectMatchesForTest(
                    Child, BP->GeneratedClass->GetDefaultObject(), Declared, Name, Export.Flags, Template)) ||
            !TestFalse(TEXT("Missing declared archetype refuses"),
                P4OriginalLevelStorageSubobjectMatchesForTest(Child, Actor, Declared, Name, Export.Flags, nullptr)) ||
            !TestFalse(TEXT("Distant source base refuses"),
                P4OriginalLevelStorageSubobjectMatchesForTest(
                    Child, Actor, USceneComponent::StaticClass(), Name, Export.Flags, Template)))
            return false;
        UObject* OtherTemplate = StaticFindObjectFast(nullptr, BP->GeneratedClass->GetDefaultObject(),
            FName(Name == TEXT("SunLight") ? TEXT("StaticSunLight") : TEXT("SunLight")));
        if (!TestNotNull(TEXT("Wrong-template control is another genuine constructor template"), OtherTemplate) ||
            !TestFalse(TEXT("Another real source template cannot authorize reuse"),
                P4OriginalLevelStorageSubobjectMatchesForTest(
                    Child, Actor, Declared, Name, Export.Flags, OtherTemplate)))
            return false;
        UObject* Ordinary = NewObject<UObject>(
            Actor, Child->GetClass(), MakeUniqueObjectName(Actor, Child->GetClass(), FName(*Name)), RF_Transient);
        if (!TestTrue(TEXT("Non-constructor object negative is genuine native storage"),
                Ordinary && Ordinary->GetClass() == Child->GetClass() && !Ordinary->HasAnyFlags(RF_DefaultSubObject)) ||
            !TestFalse(TEXT("Ordinary owned object cannot pose as a source default child"),
                P4OriginalLevelStorageSubobjectMatchesForTest(
                    Ordinary, Actor, Declared, Ordinary->GetName(), Export.Flags, Template)))
            return false;
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("source_path"), UTF8_TO_TCHAR(Path.c_str()));
        Item->SetStringField(TEXT("source_template"), UTF8_TO_TCHAR(TemplatePath.c_str()));
        Item->SetStringField(TEXT("native_class"), Child->GetClass()->GetPathName());
        Rows.Add(MakeShared<FJsonValueObject>(Item));
        ++Count;
    }
    if (!TestEqual(TEXT("All three real source default children qualify"), Count, 3))
        return false;
    Proof->SetBoolField(TEXT("passed"), !HasAnyErrors());
    Proof->SetStringField(TEXT("stage"), TEXT("complete"));
    Proof->SetStringField(TEXT("source_sha256"), Source.SourceDigest);
    Proof->SetArrayField(TEXT("components"), Rows);
    Proof->SetStringField(TEXT("scope"),
        TEXT(
            "Actual source-bound native constructor subobject reuse/refusal only; no map import/save/cook or renderer claim."));
    return !HasAnyErrors();
}
#endif

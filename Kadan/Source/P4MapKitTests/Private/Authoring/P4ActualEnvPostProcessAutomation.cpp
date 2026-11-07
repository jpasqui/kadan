#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "P4BlueprintImport.h"
#include "P4KadanImport.h"
#include "P4KadanImportSources.h"
#include "P4KadanBlueprintPlans.h"
#include "P4DecodedPropertyWriter.h"
#include "P4RecoveredPostProcessStorage.h"
#include "P4LevelImportWorld.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "P4OriginalClassComparison.h"
#include "P4MapKitCore/property_decoder.h"
#include "Components/PostProcessComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace P4ActualEnvPostProcessTests
{
using V = P4MapKit::FDecodedValue;
const FString Package = TEXT("/Game/Carrot4/Core/Graphics/System/BP_EnvController");
const FString Object = Package + TEXT(".BP_EnvController");
const TCHAR* const ForkNames[] = {TEXT("bOverride_C4IndirectLightingVolumetricLightmapIntensity"),
    TEXT("bOverride_C4IndirectLightingSpecularColor"), TEXT("bOverride_C4IndirectLightingSpecularIntensity"),
    TEXT("bOverride_DepthOfFieldTiltAngles"), TEXT("bOverride_C4DepthOfFieldMask"),
    TEXT("bOverride_C4DepthOfFieldParam0"), TEXT("bOverride_C4DepthOfFieldParam1"),
    TEXT("bOverride_C4DepthOfFieldParam2"), TEXT("bOverride_C4DepthOfFieldParam3"),
    TEXT("bOverride_C4PostProcessParam0"), TEXT("bOverride_DepthOfFieldMethod"),
    TEXT("C4IndirectLightingVolumetricLightmapIntensity"), TEXT("C4IndirectLightingSpecularColor"),
    TEXT("C4IndirectLightingSpecularIntensity"), TEXT("DepthOfFieldTiltAngles"), TEXT("C4DepthOfFieldMask"),
    TEXT("C4DepthOfFieldParam0"), TEXT("C4DepthOfFieldParam1"), TEXT("C4DepthOfFieldParam2"),
    TEXT("C4DepthOfFieldParam3"), TEXT("C4PostProcessParam0"), TEXT("DepthOfFieldMethod")};
FString Normalize(FString Path)
{
    FPaths::NormalizeFilename(Path);
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::CollapseRelativeDirectories(Path);
    FPaths::NormalizeDirectoryName(Path);
    return Path;
}
bool ReadJson(const FString& File, TSharedPtr<FJsonObject>& Json)
{
    FString Text;
    return FFileHelper::LoadFileToString(Text, *File) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Json) && Json.IsValid();
}
bool SaveJson(const FString& File, const TSharedRef<FJsonObject>& Json)
{
    FString Text;
    return FJsonSerializer::Serialize(Json, TJsonWriterFactory<TCHAR>::Create(&Text)) &&
        FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
bool Hash(const FString& File, const FString& Expected)
{
    FString Actual;
    int64 Size = 0;
    return P4HashFile(File, 1024ll * 1024 * 1024, Size, Actual) && P4ExactNativeString(Actual, Expected);
}
bool FileIdentity(const FString& File, FString& Id, FString& Written)
{
#if PLATFORM_WINDOWS
    HANDLE Handle = CreateFileW(*Normalize(File), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (Handle == INVALID_HANDLE_VALUE)
        return false;
    BY_HANDLE_FILE_INFORMATION Info = {};
    const bool Good = GetFileInformationByHandle(Handle, &Info) != 0;
    CloseHandle(Handle);
    if (!Good || (Info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return false;
    Id = FString::Printf(TEXT("%llu"), (uint64(Info.nFileIndexHigh) << 32) | uint64(Info.nFileIndexLow));
    Written = FString::Printf(
        TEXT("%llu"), (uint64(Info.ftLastWriteTime.dwHighDateTime) << 32) | uint64(Info.ftLastWriteTime.dwLowDateTime));
    return true;
#else
    return false;
#endif
}
bool GuardFile(FAutomationTestBase& Test, const FString& Path, const TSharedPtr<FJsonObject>& Row)
{
    FString Id, Written, AfterId, AfterWritten;
    bool ReadOnly = false;
    const bool HasReadOnly = Row.IsValid() && Row->TryGetBoolField(TEXT("readonly"), ReadOnly);
    return Test.TestTrue(*FString::Printf(TEXT("Proof file ID/hash/mtime remains exact: %s"), *Path),
        Row.IsValid() && FileIdentity(Path, Id, Written) &&
            P4ExactNativeString(Id, Row->GetStringField(TEXT("file_id"))) &&
            P4ExactNativeString(Written, Row->GetStringField(TEXT("mtime_filetime"))) &&
            Hash(Path, Row->GetStringField(TEXT("sha256"))) &&
            (!HasReadOnly || IFileManager::Get().IsReadOnly(*Path) == ReadOnly) &&
            FileIdentity(Path, AfterId, AfterWritten) && P4ExactNativeString(Id, AfterId) &&
            P4ExactNativeString(Written, AfterWritten));
}
TSharedRef<FJsonObject> SavedFiles(FAutomationTestBase& Test, const FString& Header)
{
    auto Files = MakeShared<FJsonObject>();
    for (const TCHAR* Extension : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
    {
        const FString File = FPaths::ChangeExtension(Header, Extension);
        if (!IFileManager::Get().FileExists(*File))
            continue;
        FString Id, Written, Sha;
        int64 Size = 0;
        if (!Test.TestTrue(TEXT("Snapshot native-saved output without rewriting it"),
                FileIdentity(File, Id, Written) && P4HashFile(File, 1024ll * 1024 * 1024, Size, Sha)))
            continue;
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("file_id"), Id);
        Row->SetStringField(TEXT("mtime_filetime"), Written);
        Row->SetStringField(TEXT("sha256"), Sha);
        Row->SetBoolField(TEXT("readonly"), IFileManager::Get().IsReadOnly(*File));
        Files->SetObjectField(File, Row);
    }
    return Files;
}
bool GuardSaved(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Files)
{
    if (!Test.TestTrue(TEXT("Native output custody is nonempty"), Files.IsValid() && Files->Values.Num() > 0))
        return false;
    for (const auto& Pair : Files->Values)
        if (!GuardFile(Test, Pair.Key, Pair.Value->AsObject()))
            return false;
    return true;
}
bool GuardEnvSaved(FAutomationTestBase& Test, const FString& Header, const TSharedPtr<FJsonObject>& Files)
{
    if (!GuardSaved(Test, Files) ||
        !Test.TestTrue(TEXT("Saved Env custody has at most four exact companion files"), Files->Values.Num() <= 4))
        return false;
    for (const auto& Pair : Files->Values)
    {
        bool ReadOnly = false;
        if (!Test.TestTrue(TEXT("Saved Env custody explicitly includes readonly flags"),
                Pair.Value->AsObject()->TryGetBoolField(TEXT("readonly"), ReadOnly)))
            return false;
    }
    int32 Found = 0;
    for (const TCHAR* Extension : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
    {
        const FString File = Normalize(FPaths::ChangeExtension(Header, Extension));
        int32 Matches = 0;
        for (const auto& Pair : Files->Values)
            if (FPaths::IsSamePath(Normalize(Pair.Key), File))
                ++Matches;
        if (!Test.TestEqual(TEXT("Exact saved Env companion set remains unchanged without aliases"), Matches,
                IFileManager::Get().FileExists(*File) ? 1 : 0))
            return false;
        Found += Matches;
    }
    return Test.TestEqual(TEXT("No arbitrary saved filename can authorize continuation"), Found, Files->Values.Num());
}
bool Captured(const TSharedPtr<FJsonValue>& Json, V& Out)
{
    if (!Json.IsValid())
        return false;
    if (Json->Type == EJson::Null)
    {
        Out.Kind = V::EKind::Null;
        return true;
    }
    if (Json->Type == EJson::Boolean)
    {
        Out = V::Atom(V::EKind::Boolean, Json->AsBool() ? "true" : "false");
        return true;
    }
    if (Json->Type == EJson::Number)
    {
        Out = V::Atom(V::EKind::Number, TCHAR_TO_UTF8(*FString::Printf(TEXT("%.17g"), Json->AsNumber())));
        return true;
    }
    if (Json->Type != EJson::Object)
        return false;
    const auto Obj = Json->AsObject();
    FString Entry;
    if (Obj->TryGetStringField(TEXT("entry"), Entry))
    {
        Out = V::Atom(V::EKind::String, TCHAR_TO_UTF8(*Entry));
        return true;
    }
    const TSharedPtr<FJsonObject>* Fields = nullptr;
    if (!Obj->TryGetObjectField(TEXT("fields"), Fields))
        return false;
    Out.Kind = V::EKind::Object;
    for (const auto& Pair : (*Fields)->Values)
    {
        V Child;
        if (!Captured(Pair.Value, Child))
            return false;
        Out.Fields.emplace_back(TCHAR_TO_UTF8(*Pair.Key), MoveTemp(Child));
    }
    return true;
}
struct FDependencyState
{
    TStrongObjectPtr<UObject> Asset;
    bool Dirty = false;
    FString Record, LegacyKey, Legacy;
};
struct FFixture
{
    TSharedPtr<FJsonObject> Manifest;
    FP4KadanSourceBinding Source;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TMap<FName, V> Defaults, Authored;
    V NativePriority, SourcePriority;
    bool HasSourcePriority = false;
    TArray<FDependencyState> Dependencies;
    FString Folder, AssetFile;
    UClass* StorageClass = nullptr;
    FP4RecoveredPostProcessStorage View;
    bool Inputs(FAutomationTestBase& Test) const
    {
        TSet<FString> Archives;
        for (const auto& Pair : Manifest->GetObjectField(TEXT("input_ledger"))->Values)
        {
            const auto Row = Pair.Value->AsObject();
            const FString Target = Normalize(FPaths::ProjectDir() / Pair.Key);
            if (!Test.TestTrue(TEXT("Successor inputs stay in this separate project without old Env/maps"),
                    FPaths::IsUnderDirectory(Target, Normalize(FPaths::ProjectDir())) &&
                        !Pair.Key.EndsWith(TEXT(".umap")) && !Pair.Key.Contains(TEXT("BP_EnvController"))) ||
                !GuardFile(Test, Target, Row))
                return false;
            FString SourceFile;
            if (Row->TryGetStringField(TEXT("source"), SourceFile))
            {
                if (!GuardFile(Test, SourceFile, Row))
                    return false;
            }
            else
            {
                const FString Archive = Row->GetStringField(TEXT("source_archive"));
                if (!Archives.Contains(Archive))
                {
                    if (!Test.TestTrue(TEXT("Consumed SDK remains bound to the exact checked archive"),
                            Hash(Archive, Manifest->GetStringField(TEXT("sdk_archive_sha256")))))
                        return false;
                    Archives.Add(Archive);
                }
                if (!Test.TestTrue(TEXT("New native runtime has a separate file ID from compiler outputs"),
                        Row->GetStringField(TEXT("file_id")) != Row->GetStringField(TEXT("compiler_file_id"))))
                    return false;
            }
        }
        for (const TCHAR* Ledger : {TEXT("prior_fixture_guard"), TEXT("source_pair_guard")})
            for (const auto& Pair : Manifest->GetObjectField(Ledger)->Values)
                if (!GuardFile(Test, Pair.Key, Pair.Value->AsObject()))
                    return false;
        for (const auto& Dependency : Dependencies)
        {
            UPackage* Outer = Dependency.Asset->GetOutermost();
            UMetaData* Metadata = Outer->GetMetaData();
            if (!Test.TestTrue(TEXT("Reused dependency records and dirty states remain exact"),
                    Outer->IsDirty() == Dependency.Dirty &&
                        P4ExactNativeString(
                            Dependency.Record, Metadata->GetValue(Dependency.Asset.Get(), TEXT("P4OriginalRecord"))) &&
                        (Dependency.LegacyKey.IsEmpty() ||
                            P4ExactNativeString(
                                Dependency.Legacy, Metadata->GetValue(Dependency.Asset.Get(), *Dependency.LegacyKey)))))
                return false;
        }
        FString Error;
        return Test.TestTrue(
            TEXT("Current actual original Env H+B stays unchanged"), P4KadanSourceBindingCurrent(Source, Error));
    }
    bool Open(FAutomationTestBase& Test)
    {
        const FString File = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ENV_POSTPROCESS_FIXTURE"));
        if (!Test.TestTrue(TEXT("The separate PP successor custody file exists"), ReadJson(File, Manifest)))
            return false;
        const FString Root = Normalize(FPaths::GetPath(File));
        if (!Test.TestTrue(TEXT("Only the distinct native-postprocess-env successor is admitted"),
                Manifest->GetStringField(TEXT("owner")) == TEXT("native-env-postprocess-proof/v1") &&
                    Manifest->GetStringField(TEXT("status")) == TEXT("prepared") &&
                    FPaths::GetCleanFilename(Root) == TEXT("native-postprocess-env") &&
                    FPaths::GetCleanFilename(FPaths::GetPath(Root)) == TEXT("private") &&
                    FPaths::IsSamePath(Normalize(FPaths::ProjectDir()), Normalize(Root / TEXT("project"))) &&
                    FPaths::IsSamePath(Manifest->GetStringField(TEXT("project")), FPaths::GetProjectFilePath()) &&
                    Manifest->GetStringField(TEXT("package")) == Package &&
                    Manifest->GetStringField(TEXT("sdk_build_id")) == TEXT("14830424") &&
                    Manifest->GetIntegerField(TEXT("maps_linked")) == 0 &&
                    !Manifest->GetBoolField(TEXT("old_env_package_linked")) &&
                    Manifest->GetIntegerField(TEXT("existing_project_installs")) == 0 &&
                    !Manifest->GetBoolField(TEXT("compiler_outputs_hardlinked")) &&
                    Manifest->GetIntegerField(TEXT("native_runtime_members_materialized")) == 8 &&
                    !Manifest->GetBoolField(TEXT("custom_renderer_qualified")) &&
                    Manifest->GetObjectField(TEXT("producer_source_pins"))->Values.Num() == 3))
            return false;
        for (const auto& Item : Manifest->GetArrayField(TEXT("sources")))
        {
            const auto Row = Item->AsObject();
            auto Asset = MakeShared<FP4KadanSourceAsset>();
            Asset->Package = Row->GetStringField(TEXT("package"));
            Asset->Header = Row->GetStringField(TEXT("header"));
            Asset->Body = Row->GetStringField(TEXT("body"));
            Asset->Kind = Asset->Package == Package ? TEXT("Blueprint") : TEXT("Reference");
            Sources.Add(Asset);
            if (!Test.TestTrue(TEXT("Exact actual source pair custody"),
                    Hash(Asset->Header, Row->GetStringField(TEXT("header_sha256"))) &&
                        Hash(Asset->Body, Row->GetStringField(TEXT("body_sha256")))))
                return false;
            if (Asset->Package != Package)
            {
                UObject* Dependency = LoadObject<UObject>(
                    nullptr, *(Asset->Package + TEXT(".") + FPackageName::GetShortName(Asset->Package)));
                if (!Test.TestTrue(TEXT("Only complete reused dependencies are loaded"),
                        Dependency && !P4IsVerifiedStandIn(Dependency)))
                    return false;
                FDependencyState State;
                State.Asset.Reset(Dependency);
                State.Dirty = Dependency->GetOutermost()->IsDirty();
                State.Record = Dependency->GetOutermost()->GetMetaData()->GetValue(
                    Dependency, TEXT("P4OriginalRecord"));
                FP4OriginalRecord Record;
                FString Why;
                if (P4ReadOriginalRecord(Dependency, Record, Why) && !Record.LegacyKey.IsEmpty())
                {
                    State.LegacyKey = Record.LegacyKey;
                    State.Legacy = Dependency->GetOutermost()->GetMetaData()->GetValue(Dependency, *State.LegacyKey);
                }
                Dependencies.Add(MoveTemp(State));
            }
        }
        if (!Test.TestEqual(TEXT("Exact original Env plus six retained source dependencies"), Sources.Num(), 7))
            return false;
        FP4KadanImportSourceScope Scope(Sources);
        FString Error;
        if (!Test.TestTrue(TEXT("Read canonical actual Env source from current explicit bindings"),
                P4KadanReadBoundSource(Package, Source, Error)))
            return false;
        if (!Test.TestEqual(TEXT("Actual source digest matches predecessor custody"), Source.SourceDigest,
                Manifest->GetObjectField(TEXT("source"))->GetStringField(TEXT("source_digest"))))
            return false;
        StorageClass = LoadClass<UPostProcessComponent>(
            nullptr, TEXT("/Script/P4StubsCore.P4RecoveredPostProcessComponent"));
        if (!Test.TestTrue(TEXT("New SDK contains the entire qualified actual native PP storage contract"),
                P4GetRecoveredPostProcessStorage(StorageClass, View, Error)))
        {
            Test.AddError(Error);
            return false;
        }
        TSharedPtr<FJsonObject> Capture;
        if (!Test.TestTrue(TEXT("Captured defaults are the exact SDK producer input"),
                Hash(Manifest->GetStringField(TEXT("defaults_file")),
                    Manifest->GetStringField(TEXT("defaults_sha256"))) &&
                    ReadJson(Manifest->GetStringField(TEXT("defaults_file")), Capture)))
            return false;
        const auto Fields = Capture->GetObjectField(TEXT("classes"))
                                ->GetObjectField(TEXT("/Script/Engine.PostProcessComponent"))
                                ->GetObjectField(TEXT("own"))
                                ->GetObjectField(TEXT("Settings"))
                                ->GetObjectField(TEXT("fields"));
        for (const TCHAR* Name : ForkNames)
        {
            V Value;
            if (!Test.TestTrue(TEXT("Each of the22 expected defaults comes from the actual original capture"),
                    Captured(Fields->TryGetField(Name), Value)))
                return false;
            Defaults.Add(FName(Name), MoveTemp(Value));
        }
        int32 NativeComponents = 0;
        for (const auto& Item : Capture->GetObjectField(TEXT("classes"))
                 ->GetObjectField(TEXT("/Script/Carrot4.EnvController"))
                 ->GetArrayField(TEXT("subobjects")))
        {
            const auto Subobject = Item->AsObject();
            if (Subobject->GetStringField(TEXT("name")) != TEXT("PostProcess"))
                continue;
            ++NativeComponents;
            const auto Changes = Subobject->GetObjectField(TEXT("changed"));
            if (!Test.TestTrue(TEXT("Native Env PP constructor has only its captured Priority default change"),
                    Changes->Values.Num() == 1 &&
                        Captured(Changes->TryGetField(TEXT("PostProcessComponent.Priority")), NativePriority)))
                return false;
        }
        if (!Test.TestEqual(TEXT("Exactly one captured native PP constructor subobject"), NativeComponents, 1))
            return false;
        const std::vector<uint8> H(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num()),
            B(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num());
        const auto Inventory = P4MapKit::ReadPackage(H, B);
        int32 Components = 0;
        if (!Test.TestTrue(TEXT("Actual Env source parses with exact canonical identity"),
                Inventory.Error.Code.empty() && FString(UTF8_TO_TCHAR(Inventory.Package.c_str())) == Package))
            return false;
        for (const auto& Export : Inventory.Exports)
            if (Export.Name == "PostProcess")
            {
                ++Components;
                std::string Class;
                if (!Test.TestTrue(TEXT("Original exact component identity remains Engine.PostProcessComponent"),
                        Inventory.Reference(Export.Class, Class) && Class == "/Script/Engine.PostProcessComponent"))
                    return false;
                P4MapKit::FPropertyDecoder Decoder(Inventory, B);
                V Values;
                auto At = Export.Offset;
                if (!Test.TestTrue(TEXT("Decode original complete PostProcess tags"),
                        Decoder.Properties(At, Export.Offset + Export.Size, Values)))
                    return false;
                const V* Settings = Values.Field("Settings");
                if (!Test.TestTrue(TEXT("Original Settings is a real tagged object"),
                        Settings && Settings->Kind == V::EKind::Object))
                    return false;
                if (const V* Priority = Values.Field("Priority"))
                {
                    SourcePriority = *Priority;
                    HasSourcePriority = true;
                }
                for (const auto& Pair : Settings->Fields)
                    if (Defaults.Contains(FName(UTF8_TO_TCHAR(Pair.first.c_str()))))
                        Authored.Add(FName(UTF8_TO_TCHAR(Pair.first.c_str())), Pair.second);
            }
        if (!Test.TestEqual(TEXT("Exactly one actual source PP component"), Components, 1) ||
            !Test.TestEqual(TEXT("Exactly nine actual authored fork fields, no source handset"), Authored.Num(), 9))
            return false;
        Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/ActualEnvPostProcess");
        IFileManager::Get().MakeDirectory(*Folder, true);
        AssetFile = Normalize(
            FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension()));
        return Inputs(Test);
    }
};

bool CheckSettings(
    FAutomationTestBase& Test, UObject* Component, const FFixture& Fixture, bool ActualSource, const TCHAR* Stage)
{
    if (!Test.TestTrue(TEXT("The PP object uses the exact genuine native child"),
            Component && Component->GetClass() == Fixture.StorageClass))
        return false;
    int32 Checked = 0;
    for (const auto& Pair : Fixture.Defaults)
    {
        const V* Source = ActualSource ? Fixture.Authored.Find(Pair.Key) : nullptr;
        V Expected = Source ? *Source : Pair.Value;
        FP4RecoveredPostProcessMember Member;
        if (!Test.TestTrue(TEXT("All22 original logical members resolve to real ForkSettings storage"),
                P4ResolveRecoveredPostProcessMember(Fixture.View, Pair.Key, Member) &&
                    Member.Container == Fixture.View.ForkSettings))
            return false;
        if (Member.OriginalDepthOfFieldMethod)
        {
            FString Native;
            if (!Test.TestTrue(TEXT("Original enum meaning maps to its distinct native enum"),
                    Expected.Kind == V::EKind::String &&
                        P4RecoveredPostProcessSourceEnum(Member, UTF8_TO_TCHAR(Expected.Text.c_str()), Native)))
                return false;
            Expected.Text = TCHAR_TO_UTF8(*Native);
        }
        const FProperty* Property = Member.Property;
        void* Scratch = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
        Property->InitializeValue(Scratch);
        ON_SCOPE_EXIT
        {
            Property->DestroyValue(Scratch);
            FMemory::Free(Scratch);
        };
        FP4DecodedPropertyWriter Writer(
            [](const FString& Path) -> UObject* { return LoadObject<UObject>(nullptr, *Path); }, true);
        if (!Test.TestTrue(
                *FString::Printf(TEXT("%s %s matches exact captured/source typed bits"), Stage, *Pair.Key.ToString()),
                Writer.Value(Property, Scratch, Expected, Pair.Key.ToString()) && !Writer.Later &&
                    Writer.NotHeld.Num() == 0 &&
                    Property->Identical(
                        P4RecoveredPostProcessMemberData(Component, Fixture.View, Member), Scratch, PPF_None)))
        {
            Test.AddError(Writer.Error);
            return false;
        }
        ++Checked;
    }
    return Test.TestEqual(TEXT("All22 observed native defaults/source overrides checked"), Checked, 22);
}
UObject* Component(UObject* Actor)
{
    const auto* Slot = Actor ? FindFProperty<FObjectPropertyBase>(Actor->GetClass(), TEXT("PostProcess")) : nullptr;
    return Slot ? Slot->GetObjectPropertyValue_InContainer(Actor) : nullptr;
}
bool CheckPriority(FAutomationTestBase& Test, UObject* PostProcess, const FFixture& Fixture)
{
    const auto* Property = FindFProperty<FFloatProperty>(PostProcess->GetClass(), TEXT("Priority"));
    float Expected = 0;
    FP4DecodedPropertyWriter Writer({}, true);
    return Test.TestTrue(TEXT("Captured native constructor Priority survives as ordinary base component storage"),
        Property && Property->GetOwnerStruct() == UPostProcessComponent::StaticClass() &&
            Writer.Value(Property, &Expected,
                Fixture.HasSourcePriority ? Fixture.SourcePriority : Fixture.NativePriority,
                TEXT("PostProcess.Priority")) &&
            Property->Identical(Property->ContainerPtrToValuePtr<void>(PostProcess), &Expected, PPF_None));
}
bool Construct(FAutomationTestBase& Test, UBlueprint* BP, const FFixture& Fixture)
{
    FP4OriginalRecord Record;
    FString Error;
    if (!Test.TestTrue(TEXT("Only a complete genuine recovered Blueprint is constructed"),
            BP && BP->GeneratedClass && !P4BlueprintReconstructionIncomplete(BP) && !P4IsVerifiedStandIn(BP) &&
                P4ReadOriginalRecord(BP, Record, Error) && Record.Package == Package &&
                Record.Kind == TEXT("blueprint") && Record.Fidelity == TEXT("recovered") &&
                Record.SourceDigestKind == TEXT("blueprint-import") && !Record.ReadOnly && Record.ReferenceOnly &&
                Record.Values.Num() == 0))
        return false;
    UObject* CDO = BP->GeneratedClass->GetDefaultObject();
    UObject* PostProcess = Component(CDO);
    if (!Test.TestTrue(TEXT("The original named component belongs to its native source CDO"),
            PostProcess && PostProcess->GetOuter() == CDO && PostProcess->GetFName() == TEXT("PostProcess")) ||
        !CheckSettings(Test, PostProcess, Fixture, true, TEXT("source-CDO")) ||
        !CheckPriority(Test, PostProcess, Fixture))
        return false;
    TStrongObjectPtr<UWorld> World(P4CreateOriginalImportWorld(
        MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("ActualEnvPostProcess")),
        GetTransientPackage()));
    if (!Test.TestNotNull(TEXT("Independent transient native construction world"), World.Get()))
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
    if (!Test.TestNotNull(TEXT("Ordinary actual source constructor spawns the Env actor"), Actor))
        return false;
    Actor->FinishSpawning(FTransform::Identity);
    for (int32 Round = 0; Round < 4; ++Round)
    {
        if (Round)
        {
            Actor->RerunConstructionScripts();
            CollectGarbage(RF_NoFlags);
        }
        UObject* Instance = Component(Actor);
        if (!Test.TestTrue(TEXT("Ordinary native component identity/outer survives construction"),
                Instance && Instance->GetOuter() == Actor && Instance->GetFName() == TEXT("PostProcess")) ||
            !CheckSettings(Test, Instance, Fixture, true, *FString::Printf(TEXT("construction-%d"), Round)) ||
            !CheckPriority(Test, Instance, Fixture))
            return false;
    }
    return Fixture.Inputs(Test);
}
bool Compare(
    FAutomationTestBase& Test, const FFixture& Fixture, const FString& Name, bool Equal, bool DefaultElision = false)
{
    const auto Result = P4CompareOriginalClassNative(Fixture.Source.HeaderPath, Fixture.Source.BodyPath);
    const FString Text = P4OriginalClassComparisonJson(Result);
    TSharedPtr<FJsonObject> Json;
    FFileHelper::SaveStringToFile(
        Text, *(Fixture.Folder / (Name + TEXT(".json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (DefaultElision)
    {
        // These controls deliberately change an authored nondefault value to the
        // real native default. UE may then omit the cooked tag. Accept ONLY the
        // strict merge's exact missing-member-set refusal, never arbitrary cook
        // failure or a claim that an omitted false/default payload was emitted.
        if (!Test.TestTrue(TEXT("Default-elision mutation is refused by the exact source/native member-set check"),
                !Equal && Result.Error == EP4OriginalClassComparisonError::CookFailed && Result.PackageKnown &&
                    Result.Package == Package &&
                    P4ExactNativeString(
                        Result.ErrorDetail, TEXT("postprocess-cook-source-native-member-set-differs")) &&
                    !Result.Same))
            return false;
        return Fixture.Inputs(Test);
    }
    if (!Test.TestTrue(TEXT("Actual source/native class cook exposes complete current facts"),
            Result.ValuesKnown && Result.ComparedKnown && Result.DefaultsReadable && Result.Compared > 0 &&
                Result.ValuesNotHeld == 0 &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Json) && Json.IsValid() &&
                !Json->GetBoolField(TEXT("values_not_held_details_truncated"))))
        return false;
    if (Equal)
    {
        const bool Good = Result.IsValid() && Result.Same && Result.Differences == 0 &&
            Result.GameDefaultFactsDigest.Len() == 64 &&
            P4ExactNativeString(Result.GameDefaultFactsDigest, Result.DefaultFactsDigest);
        if (!Test.TestTrue(TEXT("Complete actual Env native class/default cook is exact with zero Unheld"), Good))
        {
            Test.AddError(Result.ErrorDetail);
            for (const auto& Line : Result.OnlyGameSays)
                Test.AddError(TEXT("Only source: ") + Line);
            for (const auto& Line : Result.OnlyProjectSays)
                Test.AddError(TEXT("Only native: ") + Line);
            return false;
        }
    }
    else if (!Test.TestTrue(
                 TEXT("Changed actual native PP bits cause a real source/native cook mismatch, never source borrowing"),
                 !Result.Same && Result.Differences > 0 &&
                     !P4ExactNativeString(Result.GameDefaultFactsDigest, Result.DefaultFactsDigest)))
        return false;
    return Fixture.Inputs(Test);
}
bool NativeValueNegatives(FAutomationTestBase& Test, UBlueprint* BP, const FFixture& Fixture)
{
    TStrongObjectPtr<UObject> Target(Component(BP->GeneratedClass->GetDefaultObject()));
    const bool Dirty = BP->GetOutermost()->IsDirty();
    const FString Record = BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4OriginalRecord"));
    const TCHAR* Names[] = {
        TEXT("bOverride_C4PostProcessParam0"), TEXT("DepthOfFieldMethod"), TEXT("C4PostProcessParam0")};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        FP4RecoveredPostProcessMember Member;
        if (!Test.TestTrue(TEXT("Negative control uses a real actual authored PP member"),
                P4ResolveRecoveredPostProcessMember(Fixture.View, FName(Names[Index]), Member)))
            return false;
        void* Data = P4RecoveredPostProcessMemberData(Target.Get(), Fixture.View, Member);
        const FProperty* Property = Member.Property;
        if (!Test.TestNotNull(TEXT("Negative control resolves the first real native value address"), Data))
            return false;
        void* Before = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
        Property->InitializeValue(Before);
        Property->CopyCompleteValue(Before, Data);
        ON_SCOPE_EXIT
        {
            Property->CopyCompleteValue(Data, Before);
            Property->DestroyValue(Before);
            FMemory::Free(Before);
        };
        if (Index == 0)
        {
            const auto* Bit = CastFieldChecked<FBoolProperty>(Property);
            if (!Test.TestTrue(TEXT("Actual authored bool begins true before default-elision control"),
                    Bit->GetPropertyValue(Data)))
                return false;
            Bit->SetPropertyValue(Data, false);
        }
        else if (Index == 1)
        {
            const auto* Byte = CastFieldChecked<FByteProperty>(Property);
            if (!Test.TestEqual(TEXT("Actual authored enum begins Gather before default-elision control"),
                    Byte->GetPropertyValue(Data), uint8(1)))
                return false;
            Byte->SetPropertyValue(Data, 0);
        }
        else
        {
            FVector4* Value = static_cast<FVector4*>(Data);
            Value->X += .375f;
        }
        if (!Test.TestFalse(TEXT("The negative actually changes native field memory"),
                Property->Identical(Data, Before, PPF_None)) ||
            !Compare(Test, Fixture, FString(TEXT("native-change-")) + Names[Index], false, Index < 2))
            return false;
        Property->CopyCompleteValue(Data, Before);
        if (!Test.TestTrue(TEXT("The exact native field and original record/dirty state are restored"),
                Property->Identical(Data, Before, PPF_None) && BP->GetOutermost()->IsDirty() == Dirty &&
                    P4ExactNativeString(
                        Record, BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4OriginalRecord")))))
            return false;
    }
    return CheckSettings(Test, Target.Get(), Fixture, true, TEXT("negative-restored")) &&
        Compare(Test, Fixture, TEXT("class-comparison-final"), true);
}
bool PublishSourceProof(FAutomationTestBase& Test, const FFixture& Fixture, UBlueprint* BP,
    const TSharedRef<FJsonObject>& Saved, const FString& BeforeMembers, const TCHAR* Origin)
{
    auto Receipt = MakeShared<FJsonObject>();
    FString AssetSha;
    int64 Size = 0;
    if (!Test.TestTrue(TEXT("Fresh source Env has its own saved package"),
            P4HashFile(Fixture.AssetFile, 1024ll * 1024 * 1024, Size, AssetSha)))
        return false;
    Receipt->SetStringField(TEXT("owner"), TEXT("native-env-postprocess-source/v1"));
    Receipt->SetStringField(TEXT("source_digest"), Fixture.Source.SourceDigest);
    Receipt->SetStringField(TEXT("asset_sha256"), AssetSha);
    Receipt->SetStringField(
        TEXT("original_record"), BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4OriginalRecord")));
    Receipt->SetStringField(TEXT("source_members"), BeforeMembers);
    Receipt->SetObjectField(TEXT("saved_files"), Saved);
    Receipt->SetBoolField(TEXT("dirty"), BP->GetOutermost()->IsDirty());
    Receipt->SetNumberField(TEXT("process_id"), FPlatformProcess::GetCurrentProcessId());
    Receipt->SetNumberField(TEXT("expected_native_defaults"), 22);
    Receipt->SetNumberField(TEXT("actual_source_overrides"), 9);
    Receipt->SetNumberField(TEXT("values_not_held"), 0);
    Receipt->SetNumberField(TEXT("problems"), 0);
    Receipt->SetBoolField(TEXT("complete_source_class_cook_equal"), true);
    Receipt->SetBoolField(TEXT("native_value_negatives_detected"), true);
    Receipt->SetBoolField(TEXT("default_bool_enum_elision_exactly_refused"), true);
    Receipt->SetBoolField(TEXT("native_vector_cook_difference_detected"), true);
    Receipt->SetStringField(TEXT("qualification_origin"), Origin);
    Receipt->SetBoolField(TEXT("custom_renderer_qualified"), false);
    Test.AddInfo(TEXT(
        "Qualified scope is native PP storage/source construction/cook identity; Nintendo custom renderer semantics remain unqualified."));
    return Test.TestTrue(TEXT("Retain first-process evidence for separate cold gate"),
        SaveJson(Fixture.Folder / TEXT("source-import.json"), Receipt));
}

} //namespace P4ActualEnvPostProcessTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualEnvPostProcessSourceImport,
    "P4MapKit.EngineDeclarations.ActualEnvPostProcessSourceImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualEnvPostProcessSourceImport::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ENV_POSTPROCESS_FIXTURE")).IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Opt-in separate native-postprocess-env fresh proof; no existing fixture migration."));
        return true;
    }
    using namespace P4ActualEnvPostProcessTests;
    FFixture Fixture;
    if (!Fixture.Open(*this))
        return false;
    if (!TestNull(TEXT("Fresh successor Env package is unloaded"), FindPackage(nullptr, *Package)) ||
        !TestFalse(TEXT("Fresh successor has no prior Env output"), IFileManager::Get().FileExists(*Fixture.AssetFile)))
        return false;
    if (!CheckSettings(*this, Fixture.StorageClass->GetDefaultObject(), Fixture, false, TEXT("native-storage-CDO")))
        return false;
    TStrongObjectPtr<UObject> Fresh(NewObject<UObject>(GetTransientPackage(), Fixture.StorageClass));
    if (!CheckSettings(*this, Fresh.Get(), Fixture, false, TEXT("native-constructor-defaults")))
        return false;
    FP4KadanImportSourceScope Scope(Fixture.Sources);
    FP4KadanBlueprintPlans Plans;
    FString Error;
    FP4KadanImportSettings Settings;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    FP4CurrentGameRegistryPtr Reader;
    if (!TestTrue(TEXT("Ordinary source planner selects only actual Env"),
            P4KadanPlanBlueprints(
                Fixture.Sources, {Package}, Fixture.Folder / TEXT("plans"), Plans, Error, Settings, Reader) &&
                Plans.Planned.Num() == 1 && Plans.Planned[0] == Package && Plans.Refused.Num() == 0 &&
                Plans.SourceProblems.Num() == 0 && Plans.ReferenceOnlyReasons.Num() == 0))
    {
        AddError(Error);
        return false;
    }
    const auto Result = UP4BlueprintImport::RunSelectedNative(
        Plans.Folder, Fixture.Folder / TEXT("import-result.json"), {Package}, false, {}, true);
    if (!TestTrue(TEXT("Native Declare/Values/Compile/Save has zero Problems, errors or kept missing refs"),
            Result.bOk && Result.Count == 1 && Result.CompletedPackages.Num() == 1 &&
                Result.CompletedPackages.Contains(FName(*Package)) && Result.Problems.Num() == 0 &&
                Result.Errors.Num() == 0 && Result.KeptAsTheGames.Num() == 0 && !Result.bCancelled &&
                !Result.bInfrastructureFailure))
    {
        for (const auto& Why : Result.Errors)
            AddError(Why);
        for (const auto& Why : Result.Problems)
            AddError(Why);
        return false;
    }
    TStrongObjectPtr<UBlueprint> BlueprintHold(LoadObject<UBlueprint>(nullptr, *Object));
    UBlueprint* BP = BlueprintHold.Get();
    if (!TestNotNull(TEXT("Completed actual Env Blueprint is loaded"), BP))
        return false;
    const auto Saved = SavedFiles(*this, Fixture.AssetFile);
    const bool BeforeDirty = BP->GetOutermost()->IsDirty();
    const FString BeforeRecord = BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4OriginalRecord"));
    const FString BeforeMembers = BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4BlueprintSourceMembers"));
    auto VerifyFresh = [&]() -> bool
    {
        return GuardEnvSaved(*this, Fixture.AssetFile, Saved) &&
            TestTrue(TEXT("Construction/negative controls never rewrite original Blueprint records or dirty state"),
                BP->GetOutermost()->IsDirty() == BeforeDirty &&
                    P4ExactNativeString(
                        BeforeRecord, BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4OriginalRecord"))) &&
                    P4ExactNativeString(BeforeMembers,
                        BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4BlueprintSourceMembers")))) &&
            Fixture.Inputs(*this);
    };
    ON_SCOPE_EXIT
    {
        VerifyFresh();
    };
    if (!Construct(*this, BP, Fixture) || !Compare(*this, Fixture, TEXT("class-comparison-source"), true) ||
        !NativeValueNegatives(*this, BP, Fixture))
        return false;
    if (!VerifyFresh() || HasAnyErrors())
        return false;
    return PublishSourceProof(*this, Fixture, BP, Saved, BeforeMembers, TEXT("fresh-native-import"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualEnvPostProcessContinueSaved,
    "P4MapKit.EngineDeclarations.ActualEnvPostProcessContinueSaved",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualEnvPostProcessContinueSaved::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ENV_POSTPROCESS_FIXTURE")).IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Opt-in current-read continuation of exact saved Env; no planner/import/save or historical baseline claim."));
        return true;
    }
    using namespace P4ActualEnvPostProcessTests;
    FFixture Fixture;
    if (!Fixture.Open(*this))
        return false;
    if (!TestNull(TEXT("Continuation starts with saved Env unloaded"), FindPackage(nullptr, *Package)) ||
        !TestFalse(TEXT("A positive proof cannot be overwritten by continuation"),
            IFileManager::Get().FileExists(*(Fixture.Folder / TEXT("source-import.json")))))
        return false;
    TSharedPtr<FJsonObject> Custody, Import, Blocked;
    if (!TestTrue(TEXT("Explicit current-read custody is not a fabricated historical equality baseline"),
            ReadJson(Fixture.Folder / TEXT("continuation-custody.json"), Custody) &&
                Custody->GetStringField(TEXT("owner")) == TEXT("native-env-postprocess-continuation/v1") &&
                Custody->GetStringField(TEXT("package")) == Package &&
                Custody->GetStringField(TEXT("source_digest")) == Fixture.Source.SourceDigest &&
                !Custody->GetBoolField(TEXT("native_equality_qualified")) &&
                !Custody->GetBoolField(TEXT("historical_baseline_claimed")) &&
                Hash(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ENV_POSTPROCESS_FIXTURE")),
                    Custody->GetStringField(TEXT("fixture_sha256"))) &&
                Hash(Fixture.Folder / TEXT("import-result.json"),
                    Custody->GetStringField(TEXT("reviewed_import_sha256")))))
        return false;
    auto GuardCustody = [&]() -> bool
    {
        for (const auto& Pair : Custody->GetObjectField(TEXT("guard"))->Values)
            if (!GuardFile(*this, Pair.Key, Pair.Value->AsObject()))
                return false;
        return GuardEnvSaved(*this, Fixture.AssetFile, Custody->GetObjectField(TEXT("saved_files"))) &&
            Fixture.Inputs(*this);
    };
    if (!GuardCustody())
        return false;
    if (!TestTrue(TEXT("Retained native import completed exactly this Env with zero reported Problems"),
            ReadJson(Fixture.Folder / TEXT("import-result.json"), Import) && Import->GetBoolField(TEXT("ok")) &&
                Import->GetStringField(TEXT("phase")) == TEXT("done") && Import->GetIntegerField(TEXT("count")) == 1 &&
                !Import->GetBoolField(TEXT("cancelled")) && Import->GetIntegerField(TEXT("stand_ins")) == 0 &&
                Import->GetArrayField(TEXT("errors")).Num() == 0 &&
                Import->GetArrayField(TEXT("problems")).Num() == 0 &&
                Import->GetArrayField(TEXT("kept_as_the_games")).Num() == 0 &&
                Import->GetObjectField(TEXT("rows"))->Values.Num() == 1 &&
                Import->GetObjectField(TEXT("rows"))->GetObjectField(Package)->GetBoolField(TEXT("saved")) &&
                Import->GetObjectField(TEXT("rows"))->GetObjectField(Package)->GetStringField(TEXT("status")) ==
                    TEXT("complete")))
        return false;
    if (!TestTrue(TEXT("Original failed cook receipt is retained as a refusal, never as a prior positive proof"),
            ReadJson(Fixture.Folder / TEXT("class-comparison-source.json"), Blocked) &&
                !Blocked->GetBoolField(TEXT("valid")) && !Blocked->GetBoolField(TEXT("same")) &&
                Blocked->GetStringField(TEXT("package")) == Package &&
                P4ExactNativeString(Blocked->GetStringField(TEXT("error")),
                    TEXT(
                        "postprocess-cook-native-tail-relocation-unqualified: BP_EnvController_C.SkyAtmosphere1_GEN_VARIABLE"))))
        return false;
    FP4KadanImportSourceScope Scope(Fixture.Sources);
    TStrongObjectPtr<UBlueprint> Hold(LoadObject<UBlueprint>(nullptr, *Object));
    UBlueprint* BP = Hold.Get();
    if (!TestNotNull(TEXT("Only ordinary loading opens the exact saved native Blueprint"), BP))
        return false;
    const bool BeforeDirty = BP->GetOutermost()->IsDirty();
    const FString BeforeRecord = BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4OriginalRecord"));
    const FString BeforeMembers = BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4BlueprintSourceMembers"));
    auto VerifyCurrent = [&]() -> bool
    {
        return GuardCustody() &&
            TestTrue(TEXT("Continuation preserves current original record/source members/dirty state exactly"),
                BP->GetOutermost()->IsDirty() == BeforeDirty &&
                    P4ExactNativeString(
                        BeforeRecord, BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4OriginalRecord"))) &&
                    P4ExactNativeString(BeforeMembers,
                        BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4BlueprintSourceMembers"))));
    };
    ON_SCOPE_EXIT
    {
        VerifyCurrent();
    };
    if (!Construct(*this, BP, Fixture) || !Compare(*this, Fixture, TEXT("class-comparison-continued"), true) ||
        !NativeValueNegatives(*this, BP, Fixture) || !VerifyCurrent() || HasAnyErrors())
        return false;
    return PublishSourceProof(*this, Fixture, BP, Custody->GetObjectField(TEXT("saved_files")).ToSharedRef(),
        BeforeMembers, TEXT("current-saved-native-import-continuation"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualEnvPostProcessColdConstruction,
    "P4MapKit.EngineDeclarations.ActualEnvPostProcessColdConstruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualEnvPostProcessColdConstruction::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ENV_POSTPROCESS_FIXTURE")).IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Opt-in distinct-process native PP cold proof; ordinary LoadObject only."));
        return true;
    }
    using namespace P4ActualEnvPostProcessTests;
    FFixture Fixture;
    if (!Fixture.Open(*this))
        return false;
    if (!TestNull(
            TEXT("Cold successor starts with actual source Env package unloaded"), FindPackage(nullptr, *Package)))
        return false;
    TSharedPtr<FJsonObject> Receipt;
    if (!TestTrue(TEXT("Cold proof uses the exact prior fresh result in another process"),
            ReadJson(Fixture.Folder / TEXT("source-import.json"), Receipt) &&
                Receipt->GetStringField(TEXT("owner")) == TEXT("native-env-postprocess-source/v1") &&
                Receipt->GetStringField(TEXT("source_digest")) == Fixture.Source.SourceDigest &&
                uint32(Receipt->GetNumberField(TEXT("process_id"))) != FPlatformProcess::GetCurrentProcessId() &&
                Receipt->GetIntegerField(TEXT("values_not_held")) == 0 &&
                Receipt->GetIntegerField(TEXT("problems")) == 0 &&
                Receipt->GetBoolField(TEXT("native_value_negatives_detected")) &&
                Receipt->GetBoolField(TEXT("default_bool_enum_elision_exactly_refused")) &&
                Receipt->GetBoolField(TEXT("native_vector_cook_difference_detected")) &&
                Receipt->GetBoolField(TEXT("complete_source_class_cook_equal")) &&
                Hash(Fixture.AssetFile, Receipt->GetStringField(TEXT("asset_sha256"))) &&
                GuardEnvSaved(*this, Fixture.AssetFile, Receipt->GetObjectField(TEXT("saved_files")))))
        return false;
    FP4KadanImportSourceScope Scope(Fixture.Sources);
    TStrongObjectPtr<UBlueprint> BlueprintHold(LoadObject<UBlueprint>(nullptr, *Object));
    UBlueprint* BP = BlueprintHold.Get();
    if (!TestNotNull(TEXT("Cold proof ordinary load opens exactly the saved Blueprint"), BP))
        return false;
    auto VerifyCold = [&]() -> bool
    {
        return GuardEnvSaved(*this, Fixture.AssetFile, Receipt->GetObjectField(TEXT("saved_files"))) &&
            Fixture.Inputs(*this) &&
            TestTrue(TEXT("Cold constructor/cook preserves exact saved Blueprint, original record and dirty state"),
                Hash(Fixture.AssetFile, Receipt->GetStringField(TEXT("asset_sha256"))) &&
                    P4ExactNativeString(Receipt->GetStringField(TEXT("original_record")),
                        BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4OriginalRecord"))) &&
                    P4ExactNativeString(Receipt->GetStringField(TEXT("source_members")),
                        BP->GetOutermost()->GetMetaData()->GetValue(BP, TEXT("P4BlueprintSourceMembers"))) &&
                    Receipt->GetBoolField(TEXT("dirty")) == BP->GetOutermost()->IsDirty());
    };
    ON_SCOPE_EXIT
    {
        VerifyCold();
    };
    if (!VerifyCold() || !Construct(*this, BP, Fixture) ||
        !Compare(*this, Fixture, TEXT("class-comparison-cold"), true) || !VerifyCold())
        return false;
    auto Cold = MakeShared<FJsonObject>();
    Cold->SetStringField(TEXT("owner"), TEXT("native-env-postprocess-cold/v1"));
    Cold->SetStringField(TEXT("source_digest"), Fixture.Source.SourceDigest);
    Cold->SetNumberField(TEXT("expected_native_defaults"), 22);
    Cold->SetNumberField(TEXT("actual_source_overrides"), 9);
    Cold->SetNumberField(TEXT("values_not_held"), 0);
    Cold->SetBoolField(TEXT("distinct_process_load_construct_cook_exact"), true);
    Cold->SetBoolField(TEXT("custom_renderer_qualified"), false);
    if (!GuardEnvSaved(*this, Fixture.AssetFile, Receipt->GetObjectField(TEXT("saved_files"))) ||
        !Fixture.Inputs(*this) || HasAnyErrors())
        return false;
    return TestTrue(
        TEXT("Retain independent cold proof"), SaveJson(Fixture.Folder / TEXT("cold-construction.json"), Cold));
}
#endif

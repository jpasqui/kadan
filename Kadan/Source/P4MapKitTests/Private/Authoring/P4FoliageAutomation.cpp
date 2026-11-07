#include "P4FoliageContent.h"
#include "P4RecoveredFoliageType.h"
#include "P4DiagnosticCookPackageGuard.h"
#include "P4MapKitCore/package_reader.h"
#include "P4DecodedPropertyWriter.h"
#include "Editor.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Engine/StaticMesh.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace
{
// GEditor queues DataValidation for the next editor tick after SavePackage.
// Keep this private mount alive through that callback; immediate scope cleanup
// makes its saved package name unmappable before validation resolves its file.
class FFoliageFixtureMountCleanup : public IAutomationLatentCommand
{
    FString Folder;
    int32 Ticks = 3;

public:
    explicit FFoliageFixtureMountCleanup(FString InFolder) : Folder(MoveTemp(InFolder)) {}
    bool Update() override
    {
        if (Ticks-- > 0)
            return false;
        FPackageName::UnRegisterMountPoint(TEXT("/P4RecoveredFoliageReload/"), Folder);
        return true;
    }
};
void QueueFoliageFixtureMountCleanup(const FString& Folder)
{
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FFoliageFixtureMountCleanup(Folder)));
}
} //namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RecoveredFoliageCookTest, "P4MapKit.Foliage.NativeStorageCook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RecoveredFoliageCookTest::RunTest(const FString&)
{
    const FString EditorFolder = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("P4MapKit/FoliageNativeCook/editor/"));
    if (!TestTrue(TEXT("Private fixture output folder exists before mounting or saving"),
            IFileManager::Get().MakeDirectory(*EditorFolder, true)))
        return false;
    FPackageName::RegisterMountPoint(TEXT("/P4RecoveredFoliageReload/"), EditorFolder);
    ON_SCOPE_EXIT
    {
        QueueFoliageFixtureMountCleanup(EditorFolder);
    };
    FString MountedFile;
    if (!TestTrue(TEXT("Private package root maps to the retained cold-load fixture file"),
            FPackageName::TryConvertLongPackageNameToFilename(
                TEXT("/P4RecoveredFoliageReload/Type"), MountedFile, TEXT(".uasset")) &&
                FPaths::IsSamePath(MountedFile, EditorFolder / TEXT("Type.uasset"))))
        return false;
    UPackage* Package = CreatePackage(TEXT("/P4RecoveredFoliageReload/Type"));
    UClass* RecoveredClass = P4RecoveredFoliageClass();
    if (!TestNotNull(TEXT("Recovered class is registered before asset loading"), RecoveredClass))
        return false;
    TestEqual(TEXT("Recovered schema lives with the native runtime foliage parent"), RecoveredClass->GetOutermost(),
        UFoliageType_InstancedStaticMesh::StaticClass()->GetOutermost());
    TestFalse(TEXT("Runtime schema is not inherited from an editor-only package"),
        IsEditorOnlyObject(RecoveredClass, true, false));
    TestTrue(TEXT("Narrow legacy class path still resolves without renaming other Kadan objects"),
        StaticLoadClass(UFoliageType_InstancedStaticMesh::StaticClass(), nullptr,
            TEXT("/Script/Kadan.P4RecoveredFoliageType"), nullptr, LOAD_NoWarn | LOAD_Quiet) == RecoveredClass);
    // Serialized CDO imports carry the concrete foliage class, as ordinary
    // LoadObject<UFoliageType_InstancedStaticMesh> does. Asking for UObject would
    // accept the redirector itself at direct lookup and never follow its target.
    TestTrue(TEXT("Legacy saved CDO path also resolves to the runtime default"),
        StaticLoadObject(UFoliageType_InstancedStaticMesh::StaticClass(), nullptr,
            TEXT("/Script/Kadan.Default__P4RecoveredFoliageType"), nullptr,
            LOAD_NoWarn | LOAD_Quiet) == RecoveredClass->GetDefaultObject());
    UFoliageType_InstancedStaticMesh* Type = P4NewRecoveredFoliage(Package, TEXT("Type"), RF_Public | RF_Standalone);
    if (!TestNotNull(TEXT("Real native foliage constructor creates the recovered object"), Type))
        return false;
    ON_SCOPE_EXIT
    {
        Type->ClearFlags(RF_Public | RF_Standalone);
        Type->MarkPendingKill();
        Package->SetDirtyFlag(false);
        Package->MarkPendingKill();
    };
    UFoliageType_InstancedStaticMesh* BaseDefault = GetMutableDefault<UFoliageType_InstancedStaticMesh>();
    UFoliageType_InstancedStaticMesh* TwinDefault = Cast<UFoliageType_InstancedStaticMesh>(
        RecoveredClass->GetDefaultObject());
    if (!TestNotNull(TEXT("Recovered CDO uses the real native foliage type"), TwinDefault))
        return false;
    TestTrue(TEXT("Recovered class uses native constructor and reference collector"),
        RecoveredClass->ClassConstructor == UFoliageType_InstancedStaticMesh::StaticClass()->ClassConstructor &&
            RecoveredClass->ClassAddReferencedObjects ==
                UFoliageType_InstancedStaticMesh::StaticClass()->ClassAddReferencedObjects);
    const float BaseDensity = BaseDefault->Density, TwinDensity = TwinDefault->Density;
    ON_SCOPE_EXIT
    {
        BaseDefault->Density = BaseDensity;
        TwinDefault->Density = TwinDensity;
    };
    BaseDefault->Density = BaseDensity + 37.f;
    TwinDefault->Density = BaseDensity - 1.f; // An already-created CDO before the game's default correction.
    UFoliageType_InstancedStaticMesh* Late = P4NewRecoveredFoliage();
    if (!TestNotNull(TEXT("Later native object is created"), Late))
        return false;
    TestEqual(TEXT("Later instances use corrected base defaults"), Late->Density, BaseDefault->Density);
    TestEqual(
        TEXT("Twin CDO baseline catches a late base-default correction"), TwinDefault->Density, BaseDefault->Density);
    Type->Density = Late->Density;
    Late->MarkPendingKill();
    FNameProperty* GameName = FindFProperty<FNameProperty>(RecoveredClass, TEXT("FoliageTypeName"));
    FBoolProperty* GameToggle = FindFProperty<FBoolProperty>(RecoveredClass, TEXT("bUseCustomRadius"));
    FFloatProperty* GameRadius = FindFProperty<FFloatProperty>(RecoveredClass, TEXT("CustomRadius"));
    if (!TestTrue(TEXT("All three game fields have exact reflected types"),
            GameName && GameToggle && GameToggle->IsNativeBool() && GameRadius))
        return false;
    TestEqual(TEXT("Game's omitted foliage name default is retained"), GameName->GetPropertyValue_InContainer(Type),
        FName(TEXT("Default")));
    TestFalse(TEXT("Game's omitted custom radius toggle is false"), GameToggle->GetPropertyValue_InContainer(Type));
    TestEqual(TEXT("Game's omitted custom radius is zero"), GameRadius->GetPropertyValue_InContainer(Type), 0.f);
    for (const TCHAR* Name : {TEXT("FoliageTypeName"), TEXT("bUseCustomRadius"), TEXT("CustomRadius")})
    {
        FProperty* Field = Type->GetClass()->FindPropertyByName(FName(Name));
        if (!TestNotNull(TEXT("Game field has a real property"), Field))
            return false;
        TestTrue(TEXT("Game field occupies valid derived storage"),
            Field->GetOffset_ForInternal() >= UFoliageType_InstancedStaticMesh::StaticClass()->GetPropertiesSize() &&
                Field->GetOffset_ForInternal() + Field->GetSize() <= Type->GetClass()->GetPropertiesSize());
    }
    GameName->SetPropertyValue_InContainer(Type, FName(TEXT("Wood")));
    GameToggle->SetPropertyValue_InContainer(Type, true);
    GameRadius->SetPropertyValue_InContainer(Type, 12.f);
    // Exercise the actual engine's nonexported virtuals through its public
    // foliage interface. The recovered class does not supply replacements.
    UFoliageType_InstancedStaticMesh* Native = NewObject<UFoliageType_InstancedStaticMesh>();
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("Native bounds control mesh exists"), Mesh))
        return false;
    UFoliageType* RecoveredInterface = Type;
    UFoliageType* NativeInterface = Native;
    RecoveredInterface->SetSource(Mesh);
    NativeInterface->SetSource(Mesh);
    TestTrue(TEXT("Actual native source getter/setter behavior is retained"),
        RecoveredInterface->GetSource() == Mesh && NativeInterface->GetSource() == Mesh);
    RecoveredInterface->UpdateBounds();
    NativeInterface->UpdateBounds();
    TestTrue(TEXT("Actual native foliage bounds behavior is retained"),
        Type->MeshBounds.Origin == Native->MeshBounds.Origin &&
            Type->MeshBounds.BoxExtent == Native->MeshBounds.BoxExtent &&
            Type->MeshBounds.SphereRadius == Native->MeshBounds.SphereRadius &&
            Type->LowBoundOriginRadius == Native->LowBoundOriginRadius);
    FProperty* DensityField = FindFProperty<FProperty>(UFoliageType::StaticClass(), TEXT("Density"));
    const int32 BeforeChange = Type->ChangeCount, NativeBeforeChange = Native->ChangeCount;
    FPropertyChangedEvent Change(DensityField, EPropertyChangeType::ValueSet);
    RecoveredInterface->PreEditChange(DensityField);
    RecoveredInterface->PostEditChangeProperty(Change);
    NativeInterface->PreEditChange(DensityField);
    NativeInterface->PostEditChangeProperty(Change);
    TestEqual(TEXT("Actual native editor change notification is retained"), Type->ChangeCount - BeforeChange,
        Native->ChangeCount - NativeBeforeChange);
    Native->MarkPendingKill();
    // A captured source foliage type has X/Y=0x7effffff, Z=positive infinity.
    // Exercise the importer writer, actual engine cook and subsequent cold load.
    const uint32 LowerBits[] = {0x7effffffu, 0x7effffffu, 0x7f800000u};
    FMemory::Memcpy(&Type->LowBoundOriginRadius, LowerBits, sizeof(LowerBits));
    const FStructProperty* Lower = FindFProperty<FStructProperty>(
        UFoliageType::StaticClass(), TEXT("LowBoundOriginRadius"));
    const FFloatProperty* Z = Lower ? FindFProperty<FFloatProperty>(Lower->Struct, TEXT("Z")) : nullptr;
    if (!TestNotNull(TEXT("Original derived radius is reflected float storage"), Z))
        return false;
    P4MapKit::FDecodedValue Sentinel;
    Sentinel.Fields.emplace_back(
        "__native_float32_bits", P4MapKit::FDecodedValue::Atom(P4MapKit::FDecodedValue::EKind::String, "2139095040"));
    Type->LowBoundOriginRadius.Z = 0.f;
    FP4DecodedPropertyWriter Writer({}, true);
    if (!TestTrue(TEXT("Importer writes qualified sentinel without numeric conversion"),
            Writer.Value(Z, Z->ContainerPtrToValuePtr<void>(&Type->LowBoundOriginRadius), Sentinel,
                TEXT("Type LowBoundOriginRadius.Z"))))
        return false;
    TestTrue(TEXT("Importer preserves each independent source word"),
        !FMemory::Memcmp(&Type->LowBoundOriginRadius, LowerBits, sizeof(LowerBits)));
    FP4DecodedPropertyWriter Unsafe({}, true);
    float Other = 0.f;
    TestFalse(TEXT("Infinity marker cannot qualify another float field"),
        Unsafe.Value(Z, &Other, Sentinel, TEXT("Type Density")));
    const FString Base = FPaths::ProjectSavedDir() / TEXT("P4MapKit/FoliageNativeCook/type");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Base), true);
    ITargetPlatform* Platform = GetTargetPlatformManagerRef().FindTargetPlatform(TEXT("WindowsNoEditor"));
    if (!TestNotNull(TEXT("Native cook platform exists"), Platform))
        return false;
    FP4DiagnosticCookPackageGuard Guard(Package);
    Package->SetPackageFlags(PKG_FilterEditorOnly);
    P4RefreshRecoveredFoliageDefaults();
    const FSavePackageResultStruct Save = GEditor->Save(Package, Type, RF_Standalone, *(Base + TEXT(".uasset")), GError,
        nullptr, false, true, SAVE_KeepGUID | SAVE_KeepDirty | SAVE_Unversioned | SAVE_NoError | SAVE_Async, Platform,
        FDateTime::MinValue(), false);
    const bool Saved = Save.Result == ESavePackageResult::Success;
    FString Error;
    const bool Restored = Guard.AwaitAndRestore(Error);
    if (!Saved)
        AddError(FString::Printf(
            TEXT(
                "Foliage cook result=%d bytes=%lld package-flags=%08x loaded-editor-only=%d type-editor-only=%d client=%d server=%d class-flags=%08x"),
            int32(Save.Result), Save.TotalFileSize, uint32(Package->GetPackageFlags()),
            int32(Package->IsLoadedByEditorPropertiesOnly()), int32(Type->IsEditorOnly()),
            int32(Type->NeedsLoadForClient()), int32(Type->NeedsLoadForServer()),
            uint32(Type->GetClass()->ClassFlags)));
    if (!TestTrue(TEXT("Native cook finishes and source package is restored"), Saved && Restored))
    {
        if (!Error.IsEmpty())
            AddError(Error);
        return false;
    }
    TArray<uint8> Header, Body;
    if (!TestTrue(TEXT("Actual split cook bytes read back"),
            FFileHelper::LoadFileToArray(Header, *(Base + TEXT(".uasset"))) &&
                FFileHelper::LoadFileToArray(Body, *(Base + TEXT(".uexp")))))
        return false;
    std::vector<uint8> H(Header.GetData(), Header.GetData() + Header.Num()),
        B(Body.GetData(), Body.GetData() + Body.Num());
    const auto OriginalBody = B;
    if (!TestTrue(TEXT("Existing actual-byte writer restores game's class identity"),
            P4TranslateRecoveredFoliageCook(H, B, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Translation preserves every export byte"), B == OriginalBody);
    const auto Read = P4MapKit::ReadPackage(H, B);
    if (!TestTrue(
            TEXT("Translated cook has exactly one asset export"), Read.Error.Code.empty() && Read.Exports.size() == 1))
        return false;
    std::string Class, Archetype;
    Read.Reference(Read.Exports[0].Class, Class);
    Read.Reference(Read.Exports[0].Template, Archetype);
    TestEqual(TEXT("Actual cooked class is the game's class"), FString(UTF8_TO_TCHAR(Class.c_str())),
        FString(TEXT("/Script/Foliage.FoliageType_InstancedStaticMesh")));
    TestEqual(TEXT("Actual cooked CDO is the game's CDO"), FString(UTF8_TO_TCHAR(Archetype.c_str())),
        FString(TEXT("/Script/Foliage.Default__FoliageType_InstancedStaticMesh")));
    const auto Properties = P4MapKit::ReadPropertyEnvelopes(
        B, Read.Names, Read.Exports[0].Offset, Read.Exports[0].Offset + Read.Exports[0].Size);
    if (!TestTrue(TEXT("Game property tags remain readable"), Properties.Error.Code.empty()))
        return false;
    const auto LowerTag = std::find_if(Properties.Properties.begin(), Properties.Properties.end(),
        [](const P4MapKit::FPropertyEnvelope& P) { return P.Name == "LowBoundOriginRadius"; });
    if (!TestTrue(TEXT("Cook serializes captured derived radius"),
            LowerTag != Properties.Properties.end() && LowerTag->Size == sizeof(LowerBits)))
        return false;
    TestTrue(TEXT("Actual cooked radius preserves infinity and both large finite IEEE words"),
        !FMemory::Memcmp(B.data() + LowerTag->PayloadOffset, LowerBits, sizeof(LowerBits)));
    TestFalse(TEXT("Corrected omitted base default produces no extra Density tag"),
        std::any_of(Properties.Properties.begin(), Properties.Properties.end(),
            [](const P4MapKit::FPropertyEnvelope& P) { return P.Name == "Density"; }));
    for (const char* Name : {"FoliageTypeName", "bUseCustomRadius", "CustomRadius"})
        TestTrue(TEXT("Game field is physically serialized: ") + FString(UTF8_TO_TCHAR(Name)),
            std::any_of(Properties.Properties.begin(), Properties.Properties.end(),
                [&](const P4MapKit::FPropertyEnvelope& P) { return P.Name == Name; }));
    // The separate cold-process selector must load this ordinary editor file
    // through the startup-registered class, without invoking the import/factory.
    Type->Density = 129.25f; // Explicit base-property delta, independent of this process's corrected CDO.
    TestTrue(TEXT("Native editor save retains the recovered type for independent cold load"),
        GEditor->SavePackage(Package, Type, RF_Standalone, *(EditorFolder / TEXT("Type.uasset")), GError, nullptr,
            false, true, SAVE_KeepGUID | SAVE_NoError, nullptr, FDateTime::MinValue(), false));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RecoveredFoliageReloadTest, "P4MapKit.Foliage.NativeStorageReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RecoveredFoliageReloadTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_FOLIAGE_RELOAD")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "NativeStorageReload was not exercised. Run it alone in a fresh editor with P4_NATIVE_FOLIAGE_RELOAD=1 after NativeStorageCook."));
        return true;
    }
    // Run this selector in a fresh editor after NativeStorageCook. Finding the
    // class here (rather than calling its registration helper) tests startup.
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Foliage.P4RecoveredFoliageType"));
    if (!TestNotNull(TEXT("Module startup registered the recovered class before any load"), Class))
        return false;
    const FString Folder = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("P4MapKit/FoliageNativeCook/editor/"));
    if (!TestTrue(TEXT("Actual prior native editor save is present"),
            IFileManager::Get().FileExists(*(Folder / TEXT("Type.uasset")))))
        return false;
    if (!TestNull(TEXT("Cold-load package is not already loaded"),
            FindPackage(nullptr, TEXT("/P4RecoveredFoliageReload/Type"))))
        return false;
    FPackageName::RegisterMountPoint(TEXT("/P4RecoveredFoliageReload/"), Folder);
    ON_SCOPE_EXIT
    {
        QueueFoliageFixtureMountCleanup(Folder);
    };
    UFoliageType_InstancedStaticMesh* Type = LoadObject<UFoliageType_InstancedStaticMesh>(
        nullptr, TEXT("/P4RecoveredFoliageReload/Type.Type"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!TestTrue(TEXT("Ordinary cold asset load uses real foliage and the registered class"),
            Type && Type->GetClass() == Class))
        return false;
    const auto* Name = FindFProperty<FNameProperty>(Class, TEXT("FoliageTypeName"));
    const auto* Toggle = FindFProperty<FBoolProperty>(Class, TEXT("bUseCustomRadius"));
    const auto* Radius = FindFProperty<FFloatProperty>(Class, TEXT("CustomRadius"));
    if (!TestTrue(TEXT("Cold-loaded game fields retain their exact types"),
            Name && Toggle && Toggle->IsNativeBool() && Radius))
        return false;
    TestEqual(
        TEXT("Cold-load retains the authored name"), Name->GetPropertyValue_InContainer(Type), FName(TEXT("Wood")));
    TestTrue(TEXT("Cold-load retains the authored radius toggle"), Toggle->GetPropertyValue_InContainer(Type));
    TestEqual(TEXT("Cold-load retains the authored radius"), Radius->GetPropertyValue_InContainer(Type), 12.f);
    TestEqual(TEXT("Cold-load retains the authored native base property"), Type->Density, 129.25f);
    const uint32 LowerBits[] = {0x7effffffu, 0x7effffffu, 0x7f800000u};
    TestTrue(TEXT("Ordinary cold asset load preserves original infinity radius bits"),
        !FMemory::Memcmp(&Type->LowBoundOriginRadius, LowerBits, sizeof(LowerBits)));
    TestEqual(TEXT("New process CDO retains omitted game default"),
        Name->GetPropertyValue_InContainer(Class->GetDefaultObject()), FName(TEXT("Default")));
    UFoliageType* NativeInterface = Type;
    TestTrue(TEXT("Cold-load retains real native source behavior"),
        Type->Mesh && NativeInterface->GetSource() == Type->Mesh);
    return !HasAnyErrors();
}
#endif

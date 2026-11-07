#include "CoreMinimal.h"
#include "P4RecoveredLightCook.h"
#include "P4DiagnosticCookPackageGuard.h"
#include "P4MapKitCore/package_reader.h"
#include "Components/DirectionalLightComponent.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace
{
const TCHAR* Source[][2] = {{TEXT("bC4Param"), TEXT("True")}, {TEXT("C4Param0"), TEXT("0.699999988")},
    {TEXT("C4Param1"), TEXT("6")}, {TEXT("C4Param2"), TEXT("40")}, {TEXT("C4Param3"), TEXT("0.75")},
    {TEXT("C4Param8"), TEXT("0.100000001")}, {TEXT("C4Param9"), TEXT("0.200000003")}, {TEXT("C4Param10"), TEXT("0.75")},
    {TEXT("C4Param11"), TEXT("2000")}, {TEXT("C4Param12"), TEXT("500")}, {TEXT("C4Param13"), TEXT("3000")},
    {TEXT("C4Param14"), TEXT("100")}, {TEXT("C4Param15"), TEXT("5")},
    {TEXT("C4LensFlare0"), TEXT("(X=0.5,Y=0.100000001,Z=1.60000002,W=1)")},
    {TEXT("C4LensFlare1"), TEXT("(X=0.800000012,Y=0.400000006,Z=0,W=1)")},
    {TEXT("bC4ShadowBoundsOverride"), TEXT("True")}, {TEXT("C4ShadowBoundsOverride"), TEXT("(X=100,Y=100,Z=0)")}};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RecoveredDirectionalLightStorage,
    "P4MapKit.EngineDeclarations.RecoveredDirectionalLightStorage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RecoveredDirectionalLightStorage::RunTest(const FString&)
{
    UClass* Class = LoadClass<UDirectionalLightComponent>(
        nullptr, TEXT("/Script/P4StubsCore.P4RecoveredDirectionalLightComponent"));
    if (!TestNotNull(TEXT("Regenerated SDK registers native typed light storage"), Class))
        return false;
    TestEqual(TEXT("Stock light remains the exact native parent"), Class->GetSuperClass(),
        UDirectionalLightComponent::StaticClass());
    TestTrue(TEXT("UHT assembled the native reference stream"), Class->HasAnyClassFlags(CLASS_TokenStreamAssembled));
    const UObject* CDO = Class->GetDefaultObject();
    UPackage* Package = CreatePackage(TEXT("/Temp/P4RecoveredDirectionalLightControl"));
    TStrongObjectPtr<UObject> Light(
        NewObject<UObject>(Package, Class, TEXT("RecoveredLight"), RF_Public | RF_Standalone));
    if (!TestNotNull(TEXT("Ordinary native construction succeeds"), Light.Get()))
        return false;
    int32 Own = 0;
    for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        ++Own;
        const int32 Begin = It->GetOffset_ForInternal(), End = Begin + It->GetSize();
        TestTrue(*FString::Printf(TEXT("%s lies in actual UHT allocation (offset=%d size=%d class=%d parent=%d)"),
                     *It->GetName(), Begin, It->GetSize(), Class->GetStructureSize(),
                     Class->GetSuperClass()->GetStructureSize()),
            Begin >= 0 && End <= Class->GetPropertiesSize() && End <= Class->GetStructureSize() &&
                It->GetOwnerStruct() == Class);
        // The C++ ABI may reuse native superclass tail padding. UHT owns these
        // offsets; requiring >= sizeof(Super) incorrectly rejects valid layout.
        // Reflected inherited storage must still remain completely disjoint.
        for (TFieldIterator<FProperty> Parent(Class->GetSuperClass()); Parent; ++Parent)
        {
            const int32 ParentBegin = Parent->GetOffset_ForInternal(), ParentEnd = ParentBegin + Parent->GetSize();
            TestTrue(*FString::Printf(TEXT("%s never overlaps inherited %s"), *It->GetName(), *Parent->GetName()),
                End <= ParentBegin || Begin >= ParentEnd);
        }
        TestTrue(TEXT("Instance inherits exact native CDO value"), It->Identical_InContainer(CDO, Light.Get()));
        TestNull(TEXT("Stock engine class was not extended in place"),
            UDirectionalLightComponent::StaticClass()->FindPropertyByName(It->GetFName()));
    }
    TestEqual(TEXT("All observed directional-light fork members have native storage"), Own, 25);
    for (const TCHAR* Name : {TEXT("bC4Param"), TEXT("bC4ShadowBoundsOverride")})
    {
        const FBoolProperty* Field = FindFProperty<FBoolProperty>(Class, Name);
        if (!TestNotNull(TEXT("Source native bool retains actual reflected type"), Field))
            return false;
        TestTrue(TEXT("Source native bool is not a packed-bit stand-in"), Field->IsNativeBool());
        TestFalse(TEXT("Captured native bool default is false"), Field->GetPropertyValue_InContainer(CDO));
        TestTrue(
            TEXT("Source bool BlueprintReadOnly flag survives"), Field->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
    }
    const TCHAR* StructNames[] = {
        TEXT("C4Color"), TEXT("C4LensFlare0"), TEXT("C4LensFlare1"), TEXT("C4ShadowBoundsOverride")};
    const TCHAR* StructTypes[] = {TEXT("LinearColor"), TEXT("Vector4"), TEXT("Vector4"), TEXT("Vector")};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(StructNames); ++Index)
    {
        const FStructProperty* Field = FindFProperty<FStructProperty>(Class, StructNames[Index]);
        if (!TestNotNull(TEXT("Source value remains an actual native struct"), Field))
            return false;
        TestEqual(
            TEXT("Original reflected struct identity survives"), Field->Struct->GetName(), FString(StructTypes[Index]));
    }
    const FObjectProperty* Texture = FindFProperty<FObjectProperty>(Class, TEXT("C4Texture0"));
    if (!TestNotNull(TEXT("Texture field is an actual GC-traced object property"), Texture))
        return false;
    TestEqual(TEXT("Texture native class is exact"), Texture->PropertyClass->GetPathName(),
        FString(TEXT("/Script/Engine.Texture2D")));
    TestNull(TEXT("Captured native texture default is null"), Texture->GetObjectPropertyValue_InContainer(CDO));
    const TCHAR* Names[] = {TEXT("C4Param0"), TEXT("C4Param1"), TEXT("C4Param2"), TEXT("C4Param3"), TEXT("C4Param4"),
        TEXT("C4Param5"), TEXT("C4Param6"), TEXT("C4Param7"), TEXT("C4Param8"), TEXT("C4Param9"), TEXT("C4Param10"),
        TEXT("C4Param11"), TEXT("C4Param12"), TEXT("C4Param13"), TEXT("C4Param14"), TEXT("C4Param15"),
        TEXT("C4Param16"), TEXT("C4Param17")};
    const float NativeDefaults[] = {
        1.f, 1.f, 0.f, 1.f, 0.f, 1.f, 1.f, 1.f, 0.f, -1.f, 1.f, -1.f, 10000.f, 0.f, 1000.f, 1000.f, -1.f, 0.f};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
    {
        const FFloatProperty* Field = FindFProperty<FFloatProperty>(Class, Names[Index]);
        if (!TestNotNull(TEXT("Source scalar is a real native float property"), Field))
            return false;
        TestEqual(
            TEXT("Captured native default is exact"), Field->GetPropertyValue_InContainer(CDO), NativeDefaults[Index]);
        TestTrue(TEXT("Source interpolation and Blueprint edit flags survive"),
            Field->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible | CPF_Interp));
        TestFalse(TEXT("Source writable float did not acquire BlueprintReadOnly"),
            Field->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
    }
    FOutputDeviceNull Quiet;
    for (const auto& Row : Source)
    {
        FProperty* Field = Class->FindPropertyByName(Row[0]);
        if (!TestNotNull(TEXT("Each original source member exists"), Field))
            return false;
        TestNotNull(TEXT("Original source text writes native property storage"),
            Field->ImportText(Row[1], Field->ContainerPtrToValuePtr<void>(Light.Get()), PPF_None, Light.Get(), &Quiet));
    }
    const FString Directory = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("P4MapKit/EngineFields"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString File = Directory / TEXT("RecoveredLight.uasset");
    if (!TestTrue(TEXT("Ordinary editor package serializes recovered native fields"),
            UPackage::SavePackage(Package, Light.Get(), RF_Standalone, *File, GError, nullptr, false, true,
                SAVE_NoError | SAVE_KeepGUID)))
        return false;
    UPackage* ReloadPackage = LoadPackage(CreatePackage(*(TEXT("/Temp/P4RecoveredDirectionalLightReadback_") +
                                              FGuid::NewGuid().ToString(EGuidFormats::Digits))),
        *File, LOAD_NoWarn);
    UObject* Reload = ReloadPackage ? FindObject<UObject>(ReloadPackage, TEXT("RecoveredLight")) : nullptr;
    if (!TestNotNull(TEXT("A distinct saved object reloads"), Reload))
        return false;
    TestTrue(TEXT("Readback is a different native object"), Reload != Light.Get());
    TestEqual(TEXT("Readback retains actual typed storage identity"), Reload->GetClass(), Class);
    for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        TestTrue(*FString::Printf(TEXT("Native save/reload retains %s exactly"), *It->GetName()),
            It->Identical_InContainer(Light.Get(), Reload));
    ITargetPlatform* Target = GetTargetPlatformManagerRef().FindTargetPlatform(TEXT("WindowsNoEditor"));
    if (!TestNotNull(TEXT("Native cook target exists"), Target) || !TestNotNull(TEXT("Editor exists"), GEditor))
        return false;
    // Include an ordinary native sibling in this test-only cook so the actual
    // import table contains both original and storage identities. This exercises
    // the collision/remap branch without changing the saved cold fixture.
    NewObject<UDirectionalLightComponent>(Package, TEXT("StockLightControl"), RF_Public | RF_Standalone);
    const FString CookFile = Directory / TEXT("RecoveredLightCook.uasset");
    FP4DiagnosticCookPackageGuard CookGuard(Package);
    if (!TestTrue(TEXT("Diagnostic cook metadata custody is ready"), CookGuard.IsReady()))
        return false;
    if (!Target->HasEditorOnlyData())
        Package->SetPackageFlags(PKG_FilterEditorOnly);
    const bool Saved = GEditor->SavePackage(Package, nullptr, RF_Public, *CookFile, GError, nullptr, false, true,
        SAVE_NoError | SAVE_KeepGUID | SAVE_KeepDirty | SAVE_Unversioned | SAVE_Async, Target, FDateTime::MinValue(),
        false);
    FString RestoreError;
    const bool Restored = CookGuard.AwaitAndRestore(RestoreError);
    if (!TestTrue(TEXT("Actual split native cook finishes and original package state is restored"), Saved && Restored))
    {
        AddError(RestoreError);
        return false;
    }
    TArray<uint8> Head, Body;
    if (!TestTrue(TEXT("Cook wrote header"), FFileHelper::LoadFileToArray(Head, *CookFile)) ||
        !TestTrue(TEXT("Cook wrote native export body"),
            FFileHelper::LoadFileToArray(Body, *FPaths::ChangeExtension(CookFile, TEXT("uexp")))))
        return false;
    std::vector<uint8> H(Head.GetData(), Head.GetData() + Head.Num()), B(Body.GetData(), Body.GetData() + Body.Num());
    const auto Before = B;
    FString Error;
    if (!TestTrue(*FString::Printf(TEXT("Known recovered class cook translation succeeds: %s"), *Error),
            P4TranslateRecoveredLightCook(H, B, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Class import adaptation preserves every native export byte"), B == Before);
    const auto Cook = P4MapKit::ReadPackage(H, B);
    TestTrue(TEXT("Translated native package parses"), Cook.Error.Code.empty());
    bool NativeClass = false;
    for (const auto& Import : Cook.Imports)
    {
        if (Import.Name == "DirectionalLightComponent")
        {
            std::string Path;
            NativeClass |= Cook.Reference(Import.Outer, Path) && Path == "/Script/Engine";
        }
        TestFalse(TEXT("No recovered storage identity escapes native comparison"),
            Import.Name == "P4RecoveredDirectionalLightComponent" ||
                Import.Class == "P4RecoveredDirectionalLightComponent");
    }
    TestTrue(TEXT("Actual cook class identity is the original engine class"), NativeClass);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RecoveredDirectionalLightColdReload,
    "P4MapKit.EngineDeclarations.RecoveredDirectionalLightColdReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RecoveredDirectionalLightColdReload::RunTest(const FString&)
{
    const TCHAR* PackageName = TEXT("/Temp/P4RecoveredDirectionalLightControl");
    if (!TestNull(TEXT("Cold gate requires the saved fixture package to be unloaded before native load"),
            FindPackage(nullptr, PackageName)))
        return false;
    const FString File = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("P4MapKit/EngineFields/RecoveredLight.uasset"));
    if (!TestTrue(TEXT("Reuse the first gate's actual saved fixture"), IFileManager::Get().FileExists(*File)))
        return false;
    UPackage* Package = LoadPackage(CreatePackage(PackageName), *File, LOAD_NoWarn);
    UObject* Light = Package ? FindObject<UObject>(Package, TEXT("RecoveredLight")) : nullptr;
    if (!TestNotNull(TEXT("Saved native light loads in a separate process"), Light))
        return false;
    TestEqual(TEXT("Native loader independently resolves UHT storage class"), Light->GetClass()->GetPathName(),
        FString(TEXT("/Script/P4StubsCore.P4RecoveredDirectionalLightComponent")));
    TStrongObjectPtr<UObject> Expected(NewObject<UObject>(GetTransientPackage(), Light->GetClass()));
    FOutputDeviceNull Quiet;
    for (const auto& Row : Source)
    {
        FProperty* Field = Light->GetClass()->FindPropertyByName(Row[0]);
        if (!TestNotNull(TEXT("Cold native source field exists"), Field))
            return false;
        TestNotNull(TEXT("Original source value is representable"),
            Field->ImportText(
                Row[1], Field->ContainerPtrToValuePtr<void>(Expected.Get()), PPF_None, Expected.Get(), &Quiet));
    }
    int32 Count = 0;
    for (TFieldIterator<FProperty> It(Light->GetClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        ++Count;
        TestTrue(*FString::Printf(TEXT("Cold native reload retains %s exactly"), *It->GetName()),
            It->Identical_InContainer(Expected.Get(), Light));
    }
    TestEqual(TEXT("Cold native storage retains the entire observed field set"), Count, 25);
    return !HasAnyErrors();
}
#endif

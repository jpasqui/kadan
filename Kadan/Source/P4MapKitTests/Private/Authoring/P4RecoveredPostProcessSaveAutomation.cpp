// Synthetic native storage persistence controls. Original source merge/equality is
// qualified separately against actual game input; this file never invents custody.
#include "P4RecoveredPostProcessStorage.h"
#include "P4RecoveredPostProcessCook.h"
#include "P4DiagnosticCookPackageGuard.h"
#include "P4NativeHash.h"
#include "P4MapKitCore/blueprint_values.h"
#include "Components/PostProcessComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4PostProcessSaveControl
{
const TCHAR* PackageName = TEXT("/Temp/P4RecoveredPostProcessSaveControl_v1");
const TCHAR* ClassName = TEXT("/Script/P4StubsCore.P4RecoveredPostProcessComponent");
const TCHAR* ObjectName = TEXT("RecoveredPostProcess");
const TCHAR* MaskName = TEXT("SavedMask");
const TCHAR* Owner = TEXT("kadan-synthetic-postprocess-save/v1");
struct FRow
{
    const TCHAR* Name;
    const TCHAR* First;
    const TCHAR* Changed;
};
const FRow Rows[] = {{TEXT("bOverride_C4IndirectLightingVolumetricLightmapIntensity"), TEXT("True"), TEXT("False")},
    {TEXT("bOverride_C4IndirectLightingSpecularColor"), TEXT("True"), TEXT("False")},
    {TEXT("bOverride_C4IndirectLightingSpecularIntensity"), TEXT("True"), TEXT("False")},
    {TEXT("bOverride_DepthOfFieldTiltAngles"), TEXT("True"), TEXT("False")},
    {TEXT("bOverride_C4DepthOfFieldMask"), TEXT("True"), TEXT("False")},
    {TEXT("bOverride_C4DepthOfFieldParam0"), TEXT("True"), TEXT("False")},
    {TEXT("bOverride_C4DepthOfFieldParam1"), TEXT("True"), TEXT("False")},
    {TEXT("bOverride_C4DepthOfFieldParam2"), TEXT("True"), TEXT("False")},
    {TEXT("bOverride_C4DepthOfFieldParam3"), TEXT("True"), TEXT("False")},
    {TEXT("bOverride_C4PostProcessParam0"), TEXT("True"), TEXT("False")},
    {TEXT("bOverride_DepthOfFieldMethod"), TEXT("True"), TEXT("False")},
    {TEXT("C4IndirectLightingVolumetricLightmapIntensity"), TEXT("2.25"), TEXT("3.5")},
    {TEXT("C4IndirectLightingSpecularColor"), TEXT("(R=0.125,G=0.25,B=0.5,A=0.75)"),
        TEXT("(R=0.75,G=0.5,B=0.25,A=0.125)")},
    {TEXT("C4IndirectLightingSpecularIntensity"), TEXT("4.5"), TEXT("6.25")},
    {TEXT("DepthOfFieldTiltAngles"), TEXT("(X=-0.5,Y=0.25)"), TEXT("(X=0.75,Y=-0.25)")},
    {TEXT("C4DepthOfFieldMask"), nullptr, nullptr},
    {TEXT("C4DepthOfFieldParam0"), TEXT("(X=0.125,Y=0.25,Z=0.5,W=0.75)"), TEXT("(X=1.25,Y=2.5,Z=3.75,W=4.5)")},
    {TEXT("C4DepthOfFieldParam1"), TEXT("(X=2,Y=3,Z=4,W=5)"), TEXT("(X=6,Y=7,Z=8,W=9)")},
    {TEXT("C4DepthOfFieldParam2"), TEXT("(X=-1,Y=-2,Z=-3,W=-4)"), TEXT("(X=-5,Y=-6,Z=-7,W=-8)")},
    {TEXT("C4DepthOfFieldParam3"), TEXT("(X=0.5,Y=1.5,Z=2.5,W=3.5)"), TEXT("(X=4.5,Y=5.5,Z=6.5,W=7.5)")},
    {TEXT("C4PostProcessParam0"), TEXT("(X=10,Y=20,Z=30,W=40)"), TEXT("(X=50,Y=60,Z=70,W=80)")},
    {TEXT("DepthOfFieldMethod"), TEXT("P4DOFM_GatherDOF"), TEXT("P4DOFM_DefaultDOF")}};
FString Directory()
{
    return FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("P4MapKit/EngineFields/PostProcessStorage-v1"));
}
FString EditorFile()
{
    return Directory() / TEXT("EditorFixture.uasset");
}
FString CookFile()
{
    return Directory() / TEXT("CookedFixture.uasset");
}
bool ReadJson(const FString& File, TSharedPtr<FJsonObject>& Out)
{
    FString Text;
    const int64 Size = IFileManager::Get().FileSize(*File);
    return Size > 0 && Size <= 65536 && FFileHelper::LoadFileToString(Text, *File) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
}
bool WriteJson(const FString& File, const TSharedPtr<FJsonObject>& Value)
{
    FString Text;
    return FJsonSerializer::Serialize(Value.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)) &&
        FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
bool Allowed(const FString& Name)
{
    if (Name == TEXT("proof.json"))
        return true;
    for (const TCHAR* Base : {TEXT("EditorFixture"), TEXT("CookedFixture")})
        for (const TCHAR* Extension : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
            if (Name == FString(Base) + TEXT(".") + Extension)
                return true;
    return false;
}
bool Hash(const FString& File, FString& Digest)
{
    int64 Size = 0;
    return P4HashFile(File, 64ll * 1024 * 1024, Size, Digest) && Size > 0;
}
// An old or externally changed fixture cannot be overwritten. No directory/file
// cleanup is performed; every present file must belong to the exact saved ledger.
bool Owned(FAutomationTestBase& Test, TSharedPtr<FJsonObject>& Marker, bool Create)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_POSTPROCESS_SAVE_CONTROL")) != TEXT("1"))
    {
        Test.AddError(TEXT("Set P4_NATIVE_POSTPROCESS_SAVE_CONTROL=1 for the owned synthetic fixture"));
        return false;
    }
    TArray<FString> Present;
    IFileManager::Get().FindFiles(Present, *(Directory() / TEXT("*")), true, true);
    const FString MarkerFile = Directory() / TEXT("owner.json");
    if (!IFileManager::Get().FileExists(*MarkerFile))
    {
        if (!Create || Present.Num())
        {
            Test.AddError(TEXT("Unowned/nonempty synthetic PP fixture directory"));
            return false;
        }
        IFileManager::Get().MakeDirectory(*Directory(), true);
        Marker = MakeShared<FJsonObject>();
        Marker->SetStringField(TEXT("owner"), Owner);
        Marker->SetStringField(TEXT("package"), PackageName);
        Marker->SetObjectField(TEXT("files"), MakeShared<FJsonObject>());
        Marker->SetBoolField(TEXT("complete"), false);
        return WriteJson(MarkerFile, Marker);
    }
    FString GotOwner, GotPackage;
    const TSharedPtr<FJsonObject>* Files = nullptr;
    if (!ReadJson(MarkerFile, Marker) || !Marker->TryGetStringField(TEXT("owner"), GotOwner) ||
        !P4ExactNativeString(GotOwner, Owner) || !Marker->TryGetStringField(TEXT("package"), GotPackage) ||
        !P4ExactNativeString(GotPackage, PackageName) || !Marker->TryGetObjectField(TEXT("files"), Files))
    {
        Test.AddError(TEXT("Synthetic PP fixture marker identity differs"));
        return false;
    }
    if (Present.Num() != (*Files)->Values.Num() + 1)
    {
        Test.AddError(TEXT("Synthetic PP fixture has unledgered/missing paths"));
        return false;
    }
    for (const FString& Name : Present)
        if (Name != TEXT("owner.json"))
        {
            FString Expected, Actual;
            if (!Allowed(Name) || !(*Files)->TryGetStringField(Name, Expected) || !Hash(Directory() / Name, Actual) ||
                !P4ExactNativeString(Actual, Expected))
            {
                Test.AddError(TEXT("Synthetic PP fixture preimage differs: ") + Name);
                return false;
            }
        }
    return true;
}
bool RecordFiles(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Marker)
{
    TArray<FString> Present;
    IFileManager::Get().FindFiles(Present, *(Directory() / TEXT("*")), true, true);
    auto Files = MakeShared<FJsonObject>();
    for (const FString& Name : Present)
        if (Name != TEXT("owner.json"))
        {
            FString Digest;
            if (!Allowed(Name) || !Hash(Directory() / Name, Digest))
            {
                Test.AddError(TEXT("Unexpected synthetic PP output: ") + Name);
                return false;
            }
            Files->SetStringField(Name, Digest);
        }
    Marker->SetObjectField(TEXT("files"), Files);
    return WriteJson(Directory() / TEXT("owner.json"), Marker);
}
bool MaskExact(FAutomationTestBase& Test, UTexture2D* Texture)
{
    TArray64<uint8> Bytes;
    const FColor Pixel(17, 53, 97, 255);
    return Test.TestTrue(TEXT("Saved native mask retains its exact one-pixel source"),
        Texture && Texture->Source.GetSizeX() == 1 && Texture->Source.GetSizeY() == 1 &&
            Texture->Source.GetNumMips() == 1 && Texture->Source.GetMipData(Bytes, 0) &&
            Bytes.Num() == int64(sizeof(Pixel)) && FMemory::Memcmp(Bytes.GetData(), &Pixel, sizeof(Pixel)) == 0);
}
bool Storage(FAutomationTestBase& Test, UClass*& Class, FP4RecoveredPostProcessStorage& View)
{
    Class = LoadClass<UPostProcessComponent>(nullptr, ClassName);
    FString Why;
    if (!Class || !P4GetRecoveredPostProcessStorage(Class, View, Why))
    {
        Test.AddError(TEXT("Actual UHT PP storage unavailable: ") + Why);
        return false;
    }
    return true;
}
bool Fill(FAutomationTestBase& Test, UPostProcessComponent* Object, const FP4RecoveredPostProcessStorage& View,
    UTexture* Mask, bool Changed)
{
    FOutputDeviceNull Quiet;
    for (const FRow& Row : Rows)
    {
        FP4RecoveredPostProcessMember Member;
        if (!P4ResolveRecoveredPostProcessMember(View, Row.Name, Member))
        {
            Test.AddError(FString(TEXT("Missing genuine PP field: ")) + Row.Name);
            return false;
        }
        void* Address = P4RecoveredPostProcessMemberData(Object, View, Member);
        if (!Address)
            return false;
        if (!Row.First)
            CastFieldChecked<FObjectPropertyBase>(Member.Property)
                ->SetObjectPropertyValue(Address, Changed ? nullptr : Mask);
        else if (!Member.Property->ImportText(Changed ? Row.Changed : Row.First, Address, PPF_None, Object, &Quiet))
            return false;
    }
    Object->Settings.BloomIntensity = Changed ? 0.375f : 0.625f;
    Object->Settings.bOverride_BloomIntensity = true;
    return true;
}
bool Exact(FAutomationTestBase& Test, UPostProcessComponent* Object, const FP4RecoveredPostProcessStorage& View,
    UTexture* Mask, bool Changed)
{
    TStrongObjectPtr<UPostProcessComponent> Expected(
        NewObject<UPostProcessComponent>(GetTransientPackage(), Object->GetClass()));
    if (!Fill(Test, Expected.Get(), View, Mask, Changed))
        return false;
    int32 Count = 0;
    for (TFieldIterator<FProperty> It(View.ForkSettings->Struct); It; ++It)
    {
        ++Count;
        if (!Test.TestTrue(TEXT("Exact native saved PP member: ") + It->GetName(),
                It->Identical_InContainer(View.ForkSettings->ContainerPtrToValuePtr<void>(Object),
                    View.ForkSettings->ContainerPtrToValuePtr<void>(Expected.Get()))))
            return false;
    }
    return Test.TestEqual(TEXT("Every original PP extension member checked"), Count, 22) &&
        Test.TestEqual(
            TEXT("Common Settings remains exact"), Object->Settings.BloomIntensity, Changed ? 0.375f : 0.625f) &&
        Test.TestTrue(TEXT("Common native override remains set"), bool(Object->Settings.bOverride_BloomIntensity));
}
bool RawCook(FAutomationTestBase& Test, UPostProcessComponent* Object, const FP4RecoveredPostProcessStorage& View,
    bool Changed, const TSharedPtr<FJsonObject>& Marker, FString& Digest)
{
    TSharedPtr<FJsonObject> Current;
    if (!Owned(Test, Current, false))
        return false;
    ITargetPlatform* Target = GetTargetPlatformManagerRef().FindTargetPlatform(TEXT("WindowsNoEditor"));
    if (!Test.TestNotNull(TEXT("Real native cook platform"), Target) ||
        !Test.TestNotNull(TEXT("Native editor"), GEditor))
        return false;
    UPackage* Package = Object->GetOutermost();
    TStrongObjectPtr<UTexture2D> CookMask(FindObject<UTexture2D>(Package, MaskName));
    if (!Test.TestNotNull(TEXT("Owned native mask exists before texture cook preparation"), CookMask.Get()))
        return false;
    CookMask->BeginCacheForCookedPlatformData(Target);
    const double Deadline = FPlatformTime::Seconds() + 30;
    while (!CookMask->IsCachedCookedPlatformDataLoaded(Target) && FPlatformTime::Seconds() < Deadline)
        FPlatformProcess::Sleep(0.002f);
    if (!Test.TestTrue(TEXT("One-pixel owned texture reuses completed target data"),
            CookMask->IsCachedCookedPlatformDataLoaded(Target)))
        return false;
    FP4DiagnosticCookPackageGuard Guard(Package);
    if (!Guard.IsReady())
        return false;
    if (!Target->HasEditorOnlyData())
        Package->SetPackageFlags(PKG_FilterEditorOnly);
    const bool Saved = GEditor->SavePackage(Package, nullptr, RF_Public, *CookFile(), GError, nullptr, false, true,
        SAVE_NoError | SAVE_KeepGUID | SAVE_KeepDirty | SAVE_Unversioned | SAVE_Async, Target, FDateTime::MinValue(),
        false);
    FString Why;
    const bool Restored = Guard.AwaitAndRestore(Why);
    if (!RecordFiles(Test, Marker) ||
        !Test.TestTrue(TEXT("Native split cook and metadata restoration complete"), Saved && Restored))
    {
        Test.AddError(Why);
        return false;
    }
    TArray<uint8> H, B;
    if (!FFileHelper::LoadFileToArray(H, *CookFile()) ||
        !FFileHelper::LoadFileToArray(B, *FPaths::ChangeExtension(CookFile(), TEXT("uexp"))))
        return false;
    std::vector<uint8> Header(H.GetData(), H.GetData() + H.Num()), Body(B.GetData(), B.GetData() + B.Num());
    auto PackageData = P4MapKit::ReadPackage(Header, Body);
    PackageData.Package = TCHAR_TO_UTF8(PackageName);
    if (!Test.TestTrue(TEXT("Actual native cooked package parses"), PackageData.Error.Code.empty()))
        return false;
    const P4MapKit::FPackageExport* Export = nullptr;
    for (const auto& Item : PackageData.Exports)
        if (Item.Name == TCHAR_TO_UTF8(ObjectName))
        {
            if (Export)
                return false;
            Export = &Item;
        }
    if (!Export)
        return false;
    std::string Class;
    if (!Test.TestTrue(TEXT("Untranslated actual cook uses genuine native storage class"),
            PackageData.Reference(Export->Class, Class) && Class == TCHAR_TO_UTF8(ClassName)))
        return false;
    const auto Tags = P4MapKit::ReadPropertyEnvelopes(
        Body, PackageData.Names, Export->Offset, Export->Offset + Export->Size);
    if (!Tags.Error.Code.empty())
        return false;
    const P4MapKit::FPropertyEnvelope *Fork = nullptr, *Common = nullptr;
    for (const auto& Tag : Tags.Properties)
    {
        if (Tag.Name == "ForkSettings")
            Fork = &Tag;
        else if (Tag.Name == "Settings")
            Common = &Tag;
    }
    if (!Test.TestTrue(TEXT("Native cook contains both genuine struct storage regions"),
            Fork && Common && Fork->Type == "StructProperty" &&
                Fork->Detail == std::vector<std::string>{"P4RecoveredPostProcessForkSettings"} &&
                Common->Detail == std::vector<std::string>{"PostProcessSettings"}))
        return false;
    const auto Members = P4MapKit::ReadPropertyEnvelopes(
        Body, PackageData.Names, Fork->PayloadOffset, Fork->PayloadOffset + Fork->Size);
    if (!Test.TestTrue(TEXT("Native fork tags have exact bounded terminator"),
            Members.Error.Code.empty() && Members.End == Fork->PayloadOffset + Fork->Size))
        return false;
    if (!Changed &&
        !Test.TestEqual(
            TEXT("All22 deliberately nondefault native values serialize"), int32(Members.Properties.size()), 22))
        return false;
    P4MapKit::Blueprint::ValueText Decoder(PackageData, Body);
    const auto Values = Decoder.Values(*Export);
    if (!Test.TestTrue(
            TEXT("Independent tagged native value reader takes the whole fixture"), Values["unread"].Items.empty()))
        return false;
    FStructOnScope Decoded(View.ForkSettings->Struct);
    FOutputDeviceNull Quiet;
    bool ForkRead = false, CommonRead = false;
    FStructOnScope DecodedCommon(View.StockSettings->Struct);
    for (const auto& Value : Values["held"].Items)
    {
        if (Value["name"].S() == "ForkSettings")
            ForkRead = View.ForkSettings->ImportText(UTF8_TO_TCHAR(Value["text"].S().c_str()),
                           Decoded.GetStructMemory(), PPF_None, Object, &Quiet) != nullptr;
        if (Value["name"].S() == "Settings")
            CommonRead = View.StockSettings->ImportText(UTF8_TO_TCHAR(Value["text"].S().c_str()),
                             DecodedCommon.GetStructMemory(), PPF_None, Object, &Quiet) != nullptr;
    }
    if (!Test.TestTrue(TEXT("Actual cooked fork data reconstructs every current native value"),
            ForkRead &&
                View.ForkSettings->Struct->CompareScriptStruct(
                    Decoded.GetStructMemory(), View.ForkSettings->ContainerPtrToValuePtr<void>(Object), PPF_None)))
        return false;
    const FPostProcessSettings* CommonValue = reinterpret_cast<const FPostProcessSettings*>(
        DecodedCommon.GetStructMemory());
    if (!Test.TestTrue(TEXT("Actual cooked common values come from current native Settings"),
            CommonRead && CommonValue->BloomIntensity == Object->Settings.BloomIntensity &&
                CommonValue->bOverride_BloomIntensity))
        return false;
    bool Needs = false;
    if (!Test.TestTrue(TEXT("Actual native adapter detects serialized extension"),
            P4RecoveredPostProcessCookHasFork(Header, Body, Needs, Why) && Needs))
        return false;
    const auto BeforeHeader = Header, BeforeBody = Body;
    const bool Translated = P4TranslateRecoveredPostProcessCook(Header, Body, nullptr, nullptr, true, Why);
    if (!Test.TestTrue(TEXT("Synthetic cook cannot fabricate original tag-order custody"),
            !Translated && Why == TEXT("postprocess-cook-original-tag-order-required") && Header == BeforeHeader &&
                Body == BeforeBody))
        return false;
    return P4HashSourceBuffers(H.GetData(), H.Num(), B.GetData(), B.Num(), Digest);
}
} //namespace P4PostProcessSaveControl
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RecoveredPostProcessSave, "P4MapKit.EngineFields.RecoveredPostProcessSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RecoveredPostProcessSave::RunTest(const FString&)
{
    using namespace P4PostProcessSaveControl;
    TSharedPtr<FJsonObject> Marker;
    if (!Owned(*this, Marker, true))
        return false;
    if (!TestNull(TEXT("Owned normal fixture package starts unloaded"), FindPackage(nullptr, PackageName)))
        return false;
    UClass* Class = nullptr;
    FP4RecoveredPostProcessStorage View;
    if (!Storage(*this, Class, View))
        return false;
    Marker->SetBoolField(TEXT("complete"), false);
    Marker->SetNumberField(TEXT("normal_process_id"), FPlatformProcess::GetCurrentProcessId());
    if (!RecordFiles(*this, Marker))
        return false;
    UPackage* Package = CreatePackage(PackageName);
    TStrongObjectPtr<UPostProcessComponent> Instance(
        NewObject<UPostProcessComponent>(Package, Class, ObjectName, RF_Public | RF_Standalone));
    TStrongObjectPtr<UTexture2D> Texture(NewObject<UTexture2D>(Package, MaskName, RF_Public));
    const FColor Pixel(17, 53, 97, 255);
    Texture->Source.Init(1, 1, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(&Pixel));
    Texture->Source.UseHashAsGuid();
    Texture->MipGenSettings = TMGS_LeaveExistingMips;
    Texture->SRGB = false;
    if (!Fill(*this, Instance.Get(), View, Texture.Get(), false))
        return false;
    if (!TestFalse(TEXT("Synthetic mask has no standalone/root custody"),
            Texture->HasAnyFlags(RF_Standalone) || Texture->IsRooted()))
        return false;
    TWeakObjectPtr<UTexture2D> WeakTexture = Texture.Get();
    Texture.Reset();
    CollectGarbage(RF_NoFlags);
    if (!TestTrue(TEXT("Nonnull texture without standalone custody survives actual fork reference GC"),
            WeakTexture.IsValid()) ||
        !Exact(*this, Instance.Get(), View, WeakTexture.Get(), false))
        return false;
    if (!TestTrue(TEXT("Ordinary native package save succeeds"),
            UPackage::SavePackage(Package, Instance.Get(), RF_Standalone, *EditorFile(), GError, nullptr, false, true,
                SAVE_NoError | SAVE_KeepGUID)))
        return false;
    if (!RecordFiles(*this, Marker))
        return false;
    FString EditorHash;
    if (!Hash(EditorFile(), EditorHash))
        return false;
    const FString ReadbackName = TEXT("/Temp/P4RecoveredPostProcessSaveReadback_") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* ReloadPackage = LoadPackage(CreatePackage(*ReadbackName), *EditorFile(), LOAD_NoWarn);
    TStrongObjectPtr<UPostProcessComponent> Reload(
        ReloadPackage ? FindObject<UPostProcessComponent>(ReloadPackage, ObjectName) : nullptr);
    UTexture2D* ReloadMask = ReloadPackage ? FindObject<UTexture2D>(ReloadPackage, MaskName) : nullptr;
    if (!TestTrue(TEXT("Native saved readback is a distinct exact typed object"),
            Reload.IsValid() && Reload.Get() != Instance.Get() && Reload->GetClass() == Class) ||
        !TestTrue(TEXT("Saved nonnull reference resolves to the distinct saved texture"),
            ReloadMask && ReloadMask != WeakTexture.Get()) ||
        !Exact(*this, Reload.Get(), View, ReloadMask, false) || !MaskExact(*this, ReloadMask))
        return false;
    if (!TestFalse(TEXT("Saved mask retains no standalone/root custody"),
            ReloadMask->HasAnyFlags(RF_Standalone) || ReloadMask->IsRooted()))
        return false;
    TWeakObjectPtr<UTexture2D> WeakReload = ReloadMask;
    ReloadMask = nullptr;
    CollectGarbage(RF_NoFlags);
    if (!TestTrue(TEXT("Distinct saved native fork reference survives GC"), WeakReload.IsValid()) ||
        !Exact(*this, Reload.Get(), View, WeakReload.Get(), false))
        return false;
    FString FirstCook, ChangedCook;
    if (!RawCook(*this, Instance.Get(), View, false, Marker, FirstCook))
        return false;
    if (!Fill(*this, Instance.Get(), View, nullptr, true) || !Exact(*this, Instance.Get(), View, nullptr, true) ||
        !RawCook(*this, Instance.Get(), View, true, Marker, ChangedCook))
        return false;
    if (!TestTrue(
            TEXT("Changing native storage changes actual cooked bytes"), !P4ExactNativeString(FirstCook, ChangedCook)))
        return false;
    FString Preserved;
    if (!TestTrue(TEXT("Native cook/mutation does not rewrite saved cold source fixture"),
            Hash(EditorFile(), Preserved) && P4ExactNativeString(Preserved, EditorHash)))
        return false;
    auto Proof = MakeShared<FJsonObject>();
    Proof->SetStringField(
        TEXT("scope"), TEXT("synthetic-native-storage-save-cold-and-raw-cook-not-original-source-merge"));
    Proof->SetStringField(TEXT("editor_sha256"), EditorHash);
    Proof->SetStringField(TEXT("first_cook_sha256"), FirstCook);
    Proof->SetStringField(TEXT("changed_cook_sha256"), ChangedCook);
    Proof->SetNumberField(TEXT("native_fields_checked"), 22);
    Proof->SetBoolField(TEXT("source_less_translation_refused"), true);
    Proof->SetBoolField(TEXT("original_source_merge_qualified"), false);
    Proof->SetBoolField(TEXT("generated_structured_wrapper_inspected"), false);
    if (!WriteJson(Directory() / TEXT("proof.json"), Proof))
        return false;
    Marker->SetStringField(TEXT("editor_sha256"), EditorHash);
    Marker->SetBoolField(TEXT("complete"), !HasAnyErrors());
    return RecordFiles(*this, Marker) && !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RecoveredPostProcessColdReload,
    "P4MapKit.EngineFields.RecoveredPostProcessColdReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RecoveredPostProcessColdReload::RunTest(const FString&)
{
    using namespace P4PostProcessSaveControl;
    TSharedPtr<FJsonObject> Marker;
    if (!Owned(*this, Marker, false))
        return false;
    if (!TestTrue(TEXT("Cold fixture comes from completed normal gate in another process"),
            Marker->GetBoolField(TEXT("complete")) &&
                Marker->GetNumberField(TEXT("normal_process_id")) != double(FPlatformProcess::GetCurrentProcessId())) ||
        !TestNull(TEXT("Separate cold process has not loaded the native fixture"), FindPackage(nullptr, PackageName)))
        return false;
    FString Before, BeforeMarker;
    if (!Hash(Directory() / TEXT("owner.json"), BeforeMarker) || !Hash(EditorFile(), Before) ||
        !TestTrue(TEXT("Cold fixture bytes match exact normal-save SHA"),
            P4ExactNativeString(Before, Marker->GetStringField(TEXT("editor_sha256")))))
        return false;
    UClass* Class = nullptr;
    FP4RecoveredPostProcessStorage View;
    if (!Storage(*this, Class, View))
        return false;
    UPackage* Package = LoadPackage(CreatePackage(PackageName), *EditorFile(), LOAD_NoWarn);
    TStrongObjectPtr<UPostProcessComponent> Instance(
        Package ? FindObject<UPostProcessComponent>(Package, ObjectName) : nullptr);
    UTexture2D* Mask = Package ? FindObject<UTexture2D>(Package, MaskName) : nullptr;
    if (!TestTrue(TEXT("Ordinary cold loader restores the actual native storage class"),
            Instance.IsValid() && Instance->GetClass() == Class) ||
        !TestNotNull(TEXT("Cold native texture export loads"), Mask) || !MaskExact(*this, Mask) ||
        !Exact(*this, Instance.Get(), View, Mask, false))
        return false;
    if (!TestFalse(TEXT("Cold loaded texture has no standalone/root custody"),
            Mask->HasAnyFlags(RF_Standalone) || Mask->IsRooted()))
        return false;
    TWeakObjectPtr<UTexture2D> Weak = Mask;
    Mask = nullptr;
    CollectGarbage(RF_NoFlags);
    if (!TestTrue(TEXT("Cold-loaded genuine fork reference remains GC traced"), Weak.IsValid()) ||
        !Exact(*this, Instance.Get(), View, Weak.Get(), false))
        return false;
    FString After, AfterMarker;
    TSharedPtr<FJsonObject> End;
    return TestTrue(TEXT("Cold load/GC preserves all saved fixture and ledger bytes"),
               Hash(EditorFile(), After) && P4ExactNativeString(After, Before) &&
                   Hash(Directory() / TEXT("owner.json"), AfterMarker) &&
                   P4ExactNativeString(AfterMarker, BeforeMarker) && Owned(*this, End, false)) &&
        !HasAnyErrors();
}
#endif

#include "P4OriginalRecord.h"
#include "P4LevelContent.h"
#include "P4RecoveredParticleSystem.h"
#include "P4NativeHash.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/TypeData/ParticleModuleTypeDataGpu.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4RetainedParticleCustodyTests
{
bool GpuBytes(UParticleModuleTypeDataGpu* Module, TArray<uint8>& Bytes)
{
    Bytes.Reset();
    FMemoryWriter Memory(Bytes, true);
    Memory.SetWantBinaryPropertySerialization(true);
    FObjectAndNameAsStringProxyArchive Writer(Memory, false);
    for (const TCHAR* Name : {TEXT("ResourceData"), TEXT("EmitterInfo")})
    {
        const auto* Field = FindFProperty<FStructProperty>(Module->GetClass(), Name);
        if (!Field || !Field->Struct)
            return false;
        Field->Struct->SerializeBin(Writer, Field->ContainerPtrToValuePtr<void>(Module));
    }
    return !Memory.IsError() && !Writer.IsError();
}
bool SourceDigest(const FString& Header, FString& Digest)
{
    TArray<uint8> H, B;
    return FFileHelper::LoadFileToArray(H, *Header) &&
        FFileHelper::LoadFileToArray(B, *FPaths::ChangeExtension(Header, TEXT("uexp"))) &&
        P4HashSourceBuffers(H.GetData(), H.Num(), B.GetData(), B.Num(), Digest);
}
} //namespace P4RetainedParticleCustodyTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RetainedParticleNativeCustody,
    "P4MapKit.Particles.RetainedReferenceOnlyNativeCustody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RetainedParticleNativeCustody::RunTest(const FString&)
{
    using namespace P4RetainedParticleCustodyTests;
    const FString Source = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETAINED_PARTICLE_SOURCE"));
    if (Source.IsEmpty())
    {
        AddWarning(
            TEXT("P4MapKit test not run. Original retained CrackPotL asset and unchanged H+B source are required."));
        return true;
    }
    const FString Package = TEXT("/Game/Carrot4/Effects/Objects/Gimmicks/CrackPot/PS_eft_CrackPotL_Break00");
    const FString Object = Package + TEXT(".PS_eft_CrackPotL_Break00");
    const FString ExpectedBaseline = TEXT("bbd90a5ad10e539e9ffac252d66ebd3ae7876bae9607c9489f56079a11f875a4");
    const FString ExpectedSource = TEXT("2503c0c6508455710f67cb5f0389a8895f47457b3fe66e4bab126a8a766d904e");
    FString AssetFile, BeforeSource;
    TArray<uint8> BeforeFile;
    if (!TestTrue(TEXT("The actual old saved asset and exact original H+B exist before load or restoration"),
            FPackageName::TryConvertLongPackageNameToFilename(Package, AssetFile, TEXT(".uasset")) &&
                FFileHelper::LoadFileToArray(BeforeFile, *AssetFile) && SourceDigest(Source, BeforeSource) &&
                BeforeSource == ExpectedSource))
        return false;
    auto* System = LoadObject<UParticleSystem>(nullptr, *Object);
    if (!TestTrue(TEXT("The retained fixture is the exact recovered particle family"),
            System && System->GetClass() == P4RecoveredParticleSystemClass()))
        return false;
    const auto Status = P4QueryOriginalAsset(System);
    int32 Unheld = 0, CustodyNotes = 0;
    for (const auto& Note : Status.Record.Values)
    {
        if (Note.Kind == TEXT("unsupported-value"))
            ++Unheld;
        if (Note.Kind == TEXT("particle-native-source-custody"))
            ++CustodyNotes;
    }
    if (!TestTrue(
            TEXT(
                "The prior reference-only baseline is retained with all thirteen unsupported values and one custody binding"),
            Status.IsOriginal() && Status.Record.ReferenceOnly &&
                Status.Record.ComparisonSchema == TEXT("object-lines-v1") &&
                Status.Record.ComparisonDigest == ExpectedBaseline && Status.Record.SourceDigest == ExpectedSource &&
                Unheld == 13 && CustodyNotes == 1))
        return false;
    UMetaData* Metadata = System->GetOutermost()->GetMetaData();
    const FString RecordText = Metadata->GetValue(System, TEXT("P4OriginalRecord"));
    const FString CustodyText = Metadata->GetValue(System, TEXT("P4OriginalParticleCutoutCustody"));
    const FString BaselineText = Metadata->GetValue(System, TEXT("P4NativeObjectBaseline"));
    const bool Dirty = System->GetOutermost()->IsDirty();
    auto* Required = Cast<UParticleModuleRequired>(
        P4FindOriginalParticleObject(System, TEXT("PS_eft_CrackPotL_Break00.ParticleModuleRequired_12")));
    auto* Gpu = Cast<UParticleModuleTypeDataGpu>(
        P4FindOriginalParticleObject(System, TEXT("PS_eft_CrackPotL_Break00.ParticleModuleTypeDataGpu_2")));
    if (!TestTrue(TEXT("The actual GPU input/module and persisted custody are present"),
            Required && Gpu && !CustodyText.IsEmpty()))
        return false;
    const float Duration = Required->EmitterDuration;
    ON_SCOPE_EXIT
    {
        Required->EmitterDuration = Duration;
        Metadata->SetValue(System, TEXT("P4OriginalRecord"), *RecordText);
        Metadata->SetValue(System, TEXT("P4NativeObjectBaseline"), *BaselineText);
        Metadata->SetValue(System, TEXT("P4OriginalParticleCutoutCustody"), *CustodyText);
        int32 Count = 0;
        FString Why;
        if (!P4RestoreOriginalParticleCutouts(System, Count, Why))
            AddError(Why);
        System->GetOutermost()->SetDirtyFlag(Dirty);
    };
    FString Error;
    int32 Restored = 0;
    if (!TestTrue(TEXT("Explicit source-bound native restoration admits this partial original without migrating it"),
            P4RestoreOriginalParticleCutouts(System, Restored, Error) && Restored == 17))
    {
        AddError(Error);
        return false;
    }
    const auto Equal = P4DiagnoseOriginalAsset(System);
    auto Receipt = MakeShared<FJsonObject>();
    Receipt->SetStringField(TEXT("package"), Package);
    Receipt->SetStringField(TEXT("recorded_state"), ExpectedBaseline);
    Receipt->SetStringField(TEXT("restored_state"), Equal.CurrentDigest);
    Receipt->SetNumberField(TEXT("unsupported_values"), Unheld);
    Receipt->SetNumberField(TEXT("restored_cutout_modules"), Restored);
    ON_SCOPE_EXIT
    {
        Receipt->SetBoolField(TEXT("passed"), !HasAnyErrors());
        FString Text;
        FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<TCHAR>::Create(&Text));
        const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/RetainedParticleCustody/result.json");
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        if (!FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            AddError(TEXT("Native custody receipt save failed."));
    };
    if (!TestTrue(TEXT("The complete strict cook reaches the SAME original recorded baseline without rebaselining"),
            Equal.Error.IsEmpty() && Equal.Comparison == EP4OriginalComparison::Equal &&
                Equal.CurrentDigest == ExpectedBaseline))
    {
        AddError(Equal.Error);
        return false;
    }
    Metadata->SetValue(System, TEXT("P4OriginalRecord"), TEXT(""));
    Metadata->SetValue(System, TEXT("P4NativeObjectBaseline"), TEXT(""));
    if (!TestTrue(
            TEXT("Fresh publication can compute its already-applied native baseline before an original record exists"),
            !P4QueryOriginalAsset(System).IsOriginal() && P4OriginalObjectAssetState(System) == ExpectedBaseline &&
                FString(Metadata->GetValue(System, TEXT("P4OriginalRecord"))).IsEmpty()))
        return false;
    Metadata->SetValue(System, TEXT("P4OriginalRecord"), TEXT("{"));
    if (!TestTrue(TEXT("A present malformed original record fails comparison preparation closed"),
            P4OriginalObjectAssetState(System).IsEmpty()))
        return false;
    Metadata->SetValue(System, TEXT("P4OriginalRecord"), TEXT(""));
    Metadata->SetValue(System, TEXT("P4OriginalParticleCutoutCustody"), TEXT("{"));
    if (!TestTrue(TEXT("Present malformed custody fails closed even at the pre-record baseline seam"),
            P4OriginalObjectAssetState(System).IsEmpty()))
        return false;
    Metadata->SetValue(System, TEXT("P4OriginalParticleCutoutCustody"), *CustodyText);
    Metadata->SetValue(System, TEXT("P4OriginalRecord"), *RecordText);
    Metadata->SetValue(System, TEXT("P4NativeObjectBaseline"), *BaselineText);
    Receipt->SetBoolField(TEXT("fresh_publication_order_preserved"), true);
    Receipt->SetBoolField(TEXT("malformed_present_metadata_refused"), true);
    TArray<uint8> OriginalGpu, ChangedGpu;
    if (!TestTrue(TEXT("The actual restored GPU resource/emitter fields can be read independently"),
            GpuBytes(Gpu, OriginalGpu)))
        return false;
    Required->EmitterDuration = Duration + .125f;
    const auto* Resource = FindFProperty<FStructProperty>(Gpu->GetClass(), TEXT("ResourceData"));
    const auto* Bias = Resource ? FindFProperty<FStructProperty>(Resource->Struct, TEXT("ColorBias")) : nullptr;
    if (!TestTrue(TEXT("The actual native ColorBias field supports an independent changed-build control"),
            Bias && Bias->Struct && Bias->Struct->GetStructureSize() == sizeof(FVector4)))
        return false;
    // Simulate a new native build. With changed authored inputs, old custody must
    // never overwrite it, even when this fixture has no cutout texture to rebuild.
    *Bias->ContainerPtrToValuePtr<FVector4>(Resource->ContainerPtrToValuePtr<void>(Gpu)) = FVector4(13, 17, 19, 23);
    if (!TestTrue(
            TEXT("Creator input change follows current engine reconstruction rather than the original GPU capture"),
            P4RestoreOriginalParticleCutouts(System, Restored, Error) &&
                Required->EmitterDuration == Duration + .125f && GpuBytes(Gpu, ChangedGpu) &&
                ChangedGpu != OriginalGpu))
    {
        AddError(Error);
        return false;
    }
    const auto Changed = P4DiagnoseOriginalAsset(System);
    if (!TestTrue(TEXT("A genuine creator input change still fails the complete original comparison"),
            Changed.Error.IsEmpty() && Changed.Comparison == EP4OriginalComparison::Different &&
                Changed.CurrentDigest != ExpectedBaseline))
        return false;
    Required->EmitterDuration = Duration;
    if (!TestTrue(TEXT("Restoring the creator input returns only to the original strict baseline"),
            P4RestoreOriginalParticleCutouts(System, Restored, Error) &&
                P4DiagnoseOriginalAsset(System).CurrentDigest == ExpectedBaseline))
        return false;
    TArray<uint8> BeforeRefusal, AfterRefusal;
    if (!GpuBytes(Gpu, BeforeRefusal))
        return false;
    Metadata->SetValue(System, TEXT("P4OriginalParticleCutoutCustody"), *(CustodyText + TEXT(" ")));
    if (!TestTrue(TEXT("Custody text tamper refuses before changing the native GPU fields"),
            !P4RestoreOriginalParticleCutouts(System, Restored, Error) && Restored == 0 &&
                Error == TEXT("particle-custody-original-record-native-payload-binding-differs") &&
                GpuBytes(Gpu, AfterRefusal) && BeforeRefusal == AfterRefusal))
        return false;
    const FString OtherSource = FString::ChrN(64, TEXT('0'));
    const FString SourceTamper = CustodyText.Replace(*ExpectedSource, *OtherSource, ESearchCase::CaseSensitive);
    Metadata->SetValue(System, TEXT("P4OriginalParticleCutoutCustody"), *SourceTamper);
    if (!TestTrue(TEXT("A changed custody source digest refuses before changing native GPU fields"),
            SourceTamper != CustodyText && !P4RestoreOriginalParticleCutouts(System, Restored, Error) &&
                Restored == 0 && Error == TEXT("particle-custody-complete-original-record-differs") &&
                GpuBytes(Gpu, AfterRefusal) && BeforeRefusal == AfterRefusal))
        return false;
    Metadata->SetValue(System, TEXT("P4OriginalParticleCutoutCustody"), *CustodyText);
    const auto Final = P4DiagnoseOriginalAsset(System);
    FString AfterSource;
    TArray<uint8> AfterFile;
    const bool Preserved = Final.Error.IsEmpty() && Final.Comparison == EP4OriginalComparison::Equal &&
        Final.CurrentDigest == ExpectedBaseline && Final.Provenance.Record.ReferenceOnly &&
        Metadata->GetValue(System, TEXT("P4OriginalRecord")) == RecordText &&
        Metadata->GetValue(System, TEXT("P4NativeObjectBaseline")) == BaselineText &&
        SourceDigest(Source, AfterSource) && AfterSource == BeforeSource &&
        FFileHelper::LoadFileToArray(AfterFile, *AssetFile) && BeforeFile == AfterFile;
    Receipt->SetBoolField(TEXT("creator_input_protected"), true);
    Receipt->SetBoolField(TEXT("custody_tamper_refused"), true);
    Receipt->SetBoolField(TEXT("source_tamper_refused"), true);
    Receipt->SetBoolField(TEXT("files_and_original_record_preserved"), Preserved);
    return TestTrue(
        TEXT("All files, reference-only provenance, unsupported notes and prior baseline remain unchanged"), Preserved);
}
#endif

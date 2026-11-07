#include "P4OriginalAssetImport.h"
#include "P4OriginalRecord.h"
#include "P4RecoveredParticleSystem.h"
#include "P4ParticleDistributionDefaults.h"
#include "P4OriginalParticlePreflight.h"
#include "P4NativeHash.h"
#include "P4MapKitCore/package_reader.h"
#include "Particles/ParticleModuleRequired.h"
#include "Engine/Texture2D.h"
#include "DerivedDataCacheInterface.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4OriginalParticleSourceTests
{
const TCHAR* const Relative[] = {TEXT("Carrot4/Effects/Player/Npc/PS_eft_Npc_LeafLight00"),
    TEXT("Carrot4/Effects/Player/Npc/PS_eft_Npc_SurvivorAlert00"),
    TEXT("Carrot4/Effects/Player/PS_eft_Player_ReturnOut00")};
const int32 ExpectedObjects[] = {12, 38, 181};
const int32 ExpectedRequired[] = {1, 4, 17};

struct FSource
{
    TArray<uint8> Header, Body;
    P4MapKit::FPackageInventory Inventory;
    FString Digest, Package, ObjectPath;
};
bool Read(FAutomationTestBase& Test, const FString& Root, int32 Index, FSource& Source)
{
    const FString Base = Root / Relative[Index];
    Source.Package = FString(TEXT("/Game/")) + Relative[Index];
    Source.ObjectPath = Source.Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Source.Package);
    if (!Test.TestTrue(TEXT("Actual retained particle H+B source is readable and hashed"),
            FFileHelper::LoadFileToArray(Source.Header, *(Base + TEXT(".uasset"))) &&
                FFileHelper::LoadFileToArray(Source.Body, *(Base + TEXT(".uexp"))) &&
                P4HashSourceBuffers(Source.Header.GetData(), Source.Header.Num(), Source.Body.GetData(),
                    Source.Body.Num(), Source.Digest)))
        return false;
    Source.Inventory = P4MapKit::ReadPackage(
        std::vector<uint8>(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num()),
        std::vector<uint8>(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num()));
    return Test.TestTrue(TEXT("Actual source has canonical identity and bounded complete export inventory"),
        Source.Inventory.Error.Code.empty() &&
            FString(UTF8_TO_TCHAR(Source.Inventory.Package.c_str())) == Source.Package &&
            int32(Source.Inventory.Exports.size()) == ExpectedObjects[Index]);
}
bool Check(FAutomationTestBase& Test, UParticleSystem* System, const FSource& Source, int32 Index)
{
    if (!Test.TestNotNull(TEXT("Actual original particle exists as a typed ParticleSystem"), System))
        return false;
    if (!Test.TestTrue(TEXT("Actual original uses the separate real native game-field storage"),
            System->GetClass() == P4RecoveredParticleSystemClass()))
        return false;
    const auto& Package = Source.Inventory;
    const std::vector<uint8> Body(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num());
    int32 Required = 0, ExplicitGameBools = 0, Curves = 0;
    for (std::size_t Slot = 0; Slot < Package.Exports.size(); ++Slot)
    {
        const auto& Export = Package.Exports[Slot];
        std::string ClassPath, ObjectPath;
        if (!Package.Reference(Export.Class, ClassPath) || !Package.Reference(int32(Slot + 1), ObjectPath))
        {
            Test.AddError(TEXT("Actual source class/object identity is unreadable"));
            return false;
        }
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(
            Body, Package.Names, Export.Offset, Export.Offset + Export.Size);
        if (!Tags.Error.Code.empty())
        {
            Test.AddError(UTF8_TO_TCHAR(Tags.Error.Code.c_str()));
            return false;
        }
        P4MapKit::FPropertyDecoder Decoder(Package, Body);
        P4MapKit::FDecodedValue SourceValues;
        std::size_t Position = Export.Offset;
        if (!Decoder.Properties(Position, Export.Offset + Export.Size, SourceValues))
        {
            Test.AddError(UTF8_TO_TCHAR(Decoder.Error.Code.c_str()));
            return false;
        }
        UObject* LiveObject = ClassPath == "/Script/Engine.ParticleSystem"
            ? System
            : P4FindOriginalParticleObject(System, UTF8_TO_TCHAR(ObjectPath.c_str()));
        // The cook drops a baked curve and keeps its table. Every such table has its editor curve
        // again; a raw distribution without a curve holds no table either.
        int32 Stripped = 0;
        if (LiveObject)
            P4ForEachParticleRawDistribution(LiveObject,
                [&](const FP4ParticleRawDistributionPlace& Place, const FStructProperty* Raw, void* Value)
                {
                    const auto* Reference = FindFProperty<FObjectProperty>(Raw->Struct, TEXT("Distribution"));
                    const auto* Table = FindFProperty<FStructProperty>(Raw->Struct, TEXT("Table"));
                    const auto* Values = Table ? FindFProperty<FArrayProperty>(Table->Struct, TEXT("Values")) : nullptr;
                    if (!Reference || !Values)
                    {
                        ++Stripped;
                        return true;
                    }
                    if (Reference->GetObjectPropertyValue_InContainer(Value))
                    {
                        ++Curves;
                        return true;
                    }
                    FScriptArrayHelper Entries(
                        Values, Values->ContainerPtrToValuePtr<void>(Table->ContainerPtrToValuePtr<void>(Value)));
                    if (Entries.Num())
                        ++Stripped;
                    return true;
                });
        if (!Test.TestEqual(TEXT("No baked table is left without its editor curve"), Stripped, 0))
            return false;
        if (ClassPath == "/Script/Engine.ParticleModuleRequired")
        {
            ++Required;
            auto* Module = Cast<UParticleModuleRequired>(
                P4FindOriginalParticleObject(System, UTF8_TO_TCHAR(ObjectPath.c_str())));
            if (!Test.TestNotNull(TEXT("Actual original Required module identity survives"), Module))
                return false;
            TArray<uint8> Tail;
            Tail.Append(Body.data() + Tags.End, int32(Export.Offset + Export.Size - Tags.End));
            FString Error;
            if (!Test.TestTrue(TEXT("Every actual source cutout coordinate bit survives typed native readback"),
                    P4OriginalParticleRequiredTailMatches(Module, Tail, Error)))
            {
                Test.AddError(Module->GetPathName() + TEXT(": ") + Error);
                return false;
            }
        }
        if (ClassPath != "/Script/Engine.ParticleSystem")
            continue;
        TSet<FName> Authored;
        for (const auto& Tag : Tags.Properties)
        {
            if (Tag.Name != "bLinkedToActorMeshCulling" && Tag.Name != "bCheckInfiniteLifetime" &&
                Tag.Name != "bInfiniteLifetime" && Tag.Name != "bTickGroupOverwrite")
                continue;
            ++ExplicitGameBools;
            Authored.Add(FName(UTF8_TO_TCHAR(Tag.Name.c_str())));
            const auto* Field = FindFProperty<FBoolProperty>(System->GetClass(), UTF8_TO_TCHAR(Tag.Name.c_str()));
            if (!Test.TestTrue(TEXT("Actual source game field is held as its own native bool with the original value"),
                    Tag.Type == "BoolProperty" && Field && Field->IsNativeBool() &&
                        Field->GetOwnerStruct() == System->GetClass() &&
                        Field->GetPropertyValue_InContainer(System) == Tag.BoolValue))
                return false;
        }
        for (const TCHAR* Name : {TEXT("bLinkedToActorMeshCulling"), TEXT("bCheckInfiniteLifetime"),
                 TEXT("bInfiniteLifetime"), TEXT("bTickGroupOverwrite")})
        {
            if (Authored.Contains(FName(Name)))
                continue;
            const auto* Field = FindFProperty<FBoolProperty>(System->GetClass(), Name);
            if (!Test.TestTrue(TEXT("Omitted original bool keeps the captured native CDO value"),
                    Field &&
                        Field->GetPropertyValue_InContainer(System) ==
                            (FString(Name) == TEXT("bLinkedToActorMeshCulling"))))
                return false;
        }
    }
    Test.TestEqual(TEXT("All original Required modules are covered"), Required, ExpectedRequired[Index]);
    Test.TestTrue(TEXT("Actual raw distributions with editor curves were checked"), Curves > 0);
    Test.TestTrue(TEXT("Original Nintendo particle bools were actually exercised"), ExplicitGameBools > 0);
    const auto Status = P4QueryOriginalAsset(System);
    FP4OriginalValueNote Custody;
    FString CustodyError;
    int32 CustodyCount = 0;
    bool OnlyCustody = true;
    if (!P4OriginalParticleCutoutCustodyNote(System, Custody, CustodyError))
    {
        Test.AddError(CustodyError);
        return false;
    }
    for (const auto& Note : Status.Record.Values)
    {
        const bool Match = Note.Kind == Custody.Kind && Note.Object == Custody.Object &&
            Note.Property == Custody.Property && Note.GameValue == Custody.GameValue && Note.Action == Custody.Action;
        if (Match)
            ++CustodyCount;
        else
            OnlyCustody = false;
    }
    Test.TestTrue(TEXT("Actual original source custody is recorded without omissions or reference-only fallback"),
        Status.IsOriginal() && !Status.Record.ReferenceOnly && OnlyCustody && CustodyCount == 1 &&
            Status.Record.SourceDigest == Source.Digest);
    UE_LOG(LogTemp, Display, TEXT("ParticleSourceProof phase=diagnostic package=%s"), *Source.Package);
    const auto Diagnostic = P4DiagnoseOriginalAsset(System);
    Test.TestTrue(TEXT("Fresh actual native cook equals the complete recorded source baseline"),
        Diagnostic.Error.IsEmpty() && Diagnostic.Comparison == EP4OriginalComparison::Equal);
    if (!Diagnostic.Error.IsEmpty())
        Test.AddError(Diagnostic.Error);
    return !Test.HasAnyErrors();
}
bool CheckCreatorEdits(FAutomationTestBase& Test, UParticleSystem* System)
{
    auto* Module = Cast<UParticleModuleRequired>(
        P4FindOriginalParticleObject(System, System->GetName() + TEXT(".ParticleModuleRequired_1")));
    if (!Module || !Module->CutoutTexture)
    {
        Test.AddError(TEXT("Actual LeafLight creator fixture module/source texture missing"));
        return false;
    }
    const float Alpha = Module->AlphaThreshold;
    UTexture2D* Texture = Module->CutoutTexture;
    const FGuid SourceId = Texture->Source.GetId();
    const bool SystemDirty = System->GetOutermost()->IsDirty(), TextureDirty = Texture->GetOutermost()->IsDirty();
    ON_SCOPE_EXIT
    {
        Module->AlphaThreshold = Alpha;
        Module->CutoutTexture = Texture;
        // Actual native original texture importer assigns a hash GUID (UseHashAsGuid/SetId,true).
        Texture->Source.SetId(SourceId, true);
        int32 Restored = 0;
        FString RestoreError;
        if (!P4RestoreOriginalParticleCutouts(System, Restored, RestoreError))
            Test.AddError(RestoreError);
        System->GetOutermost()->SetDirtyFlag(SystemDirty);
        Texture->GetOutermost()->SetDirtyFlag(TextureDirty);
    };
    FString Error;
    int32 Restored = 0;
    Module->AlphaThreshold = Alpha + .123f;
    System->GetOutermost()->SetDirtyFlag(true);
    if (!Test.TestTrue(
            TEXT("Changed creator parameter rebuilds its own current geometry rather than the original capture"),
            P4RestoreOriginalParticleCutouts(System, Restored, Error) && Restored == 0))
    {
        Test.AddError(Error);
        return false;
    }
    Test.TestEqual(TEXT("Creator alpha edit is preserved"), Module->AlphaThreshold, Alpha + .123f);
    Test.TestTrue(TEXT("Native refresh preserves the creator's dirty flag"), System->GetOutermost()->IsDirty());
    TUniquePtr<FParticleRequiredModule> Current(Module->CreateRendererResource());
    FSubUVDerivedData Expected;
    Expected.Build(Texture, Module->SubImages_Horizontal, Module->SubImages_Vertical, Module->BoundingMode,
        Module->AlphaThreshold, Module->OpacitySourceMode);
    Test.TestTrue(TEXT("Creator parameter geometry exactly equals its separate current native Build"),
        Current->FrameData.Num() == Expected.BoundingGeometry.Num() &&
            FMemory::Memcmp(Current->FrameData.GetData(), Expected.BoundingGeometry.GetData(),
                Current->FrameData.Num() * sizeof(FVector2D)) == 0);
    Module->AlphaThreshold = Alpha;
    Texture->Source.SetId(FGuid::NewGuid(), false);
    if (!Test.TestTrue(TEXT("Changed texture Source GUID invalidates old authoritative geometry"),
            P4RestoreOriginalParticleCutouts(System, Restored, Error) && Restored == 0))
    {
        Test.AddError(Error);
        return false;
    }
    Test.TestTrue(TEXT("Creator Source GUID is retained"), Texture->Source.GetId() != SourceId);
    Current.Reset(Module->CreateRendererResource());
    Expected.Build(Texture, Module->SubImages_Horizontal, Module->SubImages_Vertical, Module->BoundingMode,
        Module->AlphaThreshold, Module->OpacitySourceMode);
    Test.TestTrue(TEXT("Changed Source geometry exactly equals its own separate native Build"),
        Current->FrameData.Num() == Expected.BoundingGeometry.Num() &&
            FMemory::Memcmp(Current->FrameData.GetData(), Expected.BoundingGeometry.GetData(),
                Current->FrameData.Num() * sizeof(FVector2D)) == 0);
    return !Test.HasAnyErrors();
}
} //namespace P4OriginalParticleSourceTests

static bool P4RunActualParticleSourceImport(
    FAutomationTestBase& Test, int32 First, int32 SourceCount, const FString& ManifestName)
{
    using namespace P4OriginalParticleSourceTests;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTICLE_SOURCE"));
    if (Root.IsEmpty())
    {
        Test.AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "ActualSourceImport not exercised: set P4_NATIVE_PARTICLE_SOURCE to the retained source Content root with real dependencies already imported in the owned native fixture project."));
        return true;
    }
    P4RecoveredParticleSystemClass();
    auto CacheManifest = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> CacheSources;
    for (int32 Index = First; Index < First + SourceCount; ++Index)
    {
        FSource Source;
        if (!Read(Test, Root, Index, Source))
            return false;
        UE_LOG(LogTemp, Display, TEXT("ParticleSourceProof phase=begin package=%s"), *Source.Package);
        FString Target;
        if (!Test.TestTrue(TEXT("Actual original destination maps to the owned project"),
                FPackageName::TryConvertLongPackageNameToFilename(Source.Package, Target, TEXT(".uasset"))))
            return false;
        UParticleSystem* System = nullptr;
        const bool Existing = FindPackage(nullptr, *Source.Package) || IFileManager::Get().FileExists(*Target);
        if (Existing)
        {
            System = LoadObject<UParticleSystem>(nullptr, *Source.ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
            const auto Status = P4QueryOriginalAsset(System);
            if (!Test.TestTrue(
                    TEXT(
                        "Resume accepts only a complete matching original with unchanged current source; never foreign or failed assets"),
                    System && System->GetClass() == P4RecoveredParticleSystemClass() && Status.IsOriginal() &&
                        !Status.Record.ReferenceOnly && Status.Record.SourceDigest == Source.Digest &&
                        Status.Record.Package == Source.Package &&
                        Status.Record.Class == System->GetClass()->GetPathName()))
                return false;
            const auto Diagnostic = P4DiagnoseOriginalAsset(System);
            if (!Test.TestTrue(
                    TEXT(
                        "Resume proves the existing actual asset's complete native state before reuse; no edits are rebaselined"),
                    Diagnostic.Error.IsEmpty() && Diagnostic.Comparison == EP4OriginalComparison::Equal))
            {
                Test.AddError(Diagnostic.Error);
                return false;
            }
            Test.AddInfo(TEXT("Reused already source/cook-qualified actual particle: ") + Source.Package);
        }
        else
        {
            UE_LOG(LogTemp, Display, TEXT("ParticleSourceProof phase=import package=%s"), *Source.Package);
            const auto Imported = P4ImportOriginalAssetNative(
                Root / Relative[Index] + TEXT(".uasset"), FString(), true, false);
            const FString ReceiptDirectory = FPaths::ProjectSavedDir() / TEXT("P4MapKit/Particles");
            if (!Test.TestTrue(TEXT("The actual import receipt is written in the reusable owned output folder"),
                    IFileManager::Get().MakeDirectory(*ReceiptDirectory, true) &&
                        FFileHelper::SaveStringToFile(P4OriginalAssetImportJson(Imported),
                            *(ReceiptDirectory /
                                (FPackageName::GetLongPackageAssetName(Source.Package) + TEXT(".import.json"))),
                            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
                return false;
            if (!Test.TestTrue(TEXT("Actual particle strict native import/save and complete source comparison succeed"),
                    Imported.IsValid()))
            {
                Test.AddError(Imported.ErrorDetail);
                return false;
            }
            Test.TestTrue(TEXT("The actual asset is new, saved, and fully source-compared"),
                Imported.Created && Imported.Saved && Imported.ComparedKnown && Imported.Compared > 0);
            Test.TestFalse(TEXT("Actual import uses no reference-only fallback"), Imported.ReferenceOnly);
            Test.TestEqual(TEXT("No actual tagged properties are omitted"), Imported.ValuesNotHeldCount, 0);
            Test.TestEqual(TEXT("No actual native bytes are omitted"), Imported.NativeBytesNotHeld, 0);
            System = Cast<UParticleSystem>(Imported.Asset);
        }
        UE_LOG(LogTemp, Display, TEXT("ParticleSourceProof phase=check package=%s"), *Source.Package);
        if (!Check(Test, System, Source, Index))
            return false;
        if (Index == 0 && !CheckCreatorEdits(Test, System))
            return false;
        // Creator edit fixture restores all live fields/resources before the final original proof.
        if (Index == 0 && !Check(Test, System, Source, Index))
            return false;
        TArray<FString> CacheKeys;
        FString CacheError;
        if (!P4OriginalParticleCutoutCacheKeys(System, CacheKeys, CacheError))
        {
            Test.AddError(CacheError);
            return false;
        }
        auto CacheRow = MakeShared<FJsonObject>();
        CacheRow->SetStringField(TEXT("package"), Source.Package);
        CacheRow->SetStringField(TEXT("source_digest"), Source.Digest);
        TArray<TSharedPtr<FJsonValue>> Keys;
        for (const FString& Key : CacheKeys)
            Keys.Add(MakeShared<FJsonValueString>(Key));
        CacheRow->SetArrayField(TEXT("keys"), Keys);
        CacheSources.Add(MakeShared<FJsonValueObject>(CacheRow));
        FSource After;
        if (!Read(Test, Root, Index, After) ||
            !Test.TestEqual(TEXT("Original H+B custody remains unchanged"), After.Digest, Source.Digest))
            return false;
    }
    CacheManifest->SetNumberField(TEXT("schema"), 1);
    CacheManifest->SetArrayField(TEXT("sources"), CacheSources);
    FString CacheText;
    FJsonSerializer::Serialize(
        CacheManifest, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&CacheText));
    Test.TestTrue(TEXT("Exact original-only cache keys are retained for a cold test with no original cached geometry"),
        FFileHelper::SaveStringToFile(CacheText,
            *(FPaths::ProjectSavedDir() / TEXT("P4MapKit/Particles") / ManifestName),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return !Test.HasAnyErrors();
}

static bool P4RunActualParticleSourceReload(
    FAutomationTestBase& Test, int32 First, int32 SourceCount, const FString& ManifestName)
{
    using namespace P4OriginalParticleSourceTests;
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTICLE_COLD_RELOAD")) != TEXT("1"))
    {
        Test.AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "ActualSourceColdReload not exercised: run alone in a fresh editor with P4_NATIVE_PARTICLE_COLD_RELOAD=1 after ActualSourceImport."));
        return true;
    }
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTICLE_SOURCE"));
    P4RecoveredParticleSystemClass();
    FString CacheText;
    TSharedPtr<FJsonObject> CacheManifest;
    const TArray<TSharedPtr<FJsonValue>>* CacheRows = nullptr;
    double Schema = 0;
    if (!Test.TestTrue(TEXT("Cold source proof requires the bounded exact key manifest from the source import"),
            FFileHelper::LoadFileToString(
                CacheText, *(FPaths::ProjectSavedDir() / TEXT("P4MapKit/Particles") / ManifestName)) &&
                CacheText.Len() <= 1024 * 1024 &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CacheText), CacheManifest) &&
                CacheManifest.IsValid() && CacheManifest->TryGetNumberField(TEXT("schema"), Schema) && Schema == 1 &&
                CacheManifest->TryGetArrayField(TEXT("sources"), CacheRows) && CacheRows->Num() == SourceCount))
        return false;
    // Clear the ORIGINAL geometry only, leaving every unrelated cache entry alone. UE's public
    // cache API has no guaranteed immediate Remove (MarkTransient is only a purge hint). Replace
    // these exact original keys with a valid-size all-zero adversarial array and verify it before
    // loading any particle. A cold pass therefore cannot depend on a warm original DDC payload.
    TSet<FString> Packages, CacheKeys;
    for (const auto& Value : *CacheRows)
    {
        if (!Value.IsValid() || Value->Type != EJson::Object)
        {
            Test.AddError(TEXT("Cold cache source row unreadable"));
            return false;
        }
        auto Row = Value->AsObject();
        FString Package, Digest;
        const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
        if (!Row->TryGetStringField(TEXT("package"), Package) ||
            !Row->TryGetStringField(TEXT("source_digest"), Digest) || !Row->TryGetArrayField(TEXT("keys"), Keys) ||
            Keys->Num() > 4096 || Packages.Contains(Package))
            return false;
        int32 SourceIndex = INDEX_NONE;
        for (int32 Index = First; Index < First + SourceCount; ++Index)
            if (Package == FString(TEXT("/Game/")) + Relative[Index])
                SourceIndex = Index;
        FSource Source;
        if (SourceIndex == INDEX_NONE || !Read(Test, Root, SourceIndex, Source) || Source.Digest != Digest)
            return false;
        Packages.Add(Package);
        for (const auto& ValueKey : *Keys)
        {
            FString Key;
            if (!ValueKey.IsValid() || !ValueKey->TryGetString(Key) || !Key.StartsWith(TEXT("SUBUV_")) ||
                Key.Len() > 1024)
                return false;
            CacheKeys.Add(Key);
        }
    }
    if (!Test.TestTrue(
            TEXT("All selected actual source identities and their filled cutout keys are bound before cache clear"),
            Packages.Num() == SourceCount && CacheKeys.Num() > 0))
        return false;
    // A fresh editor alone is insufficient: generated class defaults can eagerly load
    // these assets. Refuse before touching any cache key if either source is already held.
    for (const FString& Package : Packages)
        if (!Test.TestNull(TEXT("Every selected particle package must be unloaded BEFORE any cache poisoning"),
                FindPackage(nullptr, *Package)))
            return false;
    for (const FString& Key : CacheKeys)
    {
        TArray<uint8> Wrong;
        Wrong.SetNumZeroed(64);
        GetDerivedDataCacheRef().Put(*Key, Wrong, TEXT("P4OriginalParticleColdSourceProof"), true);
        TArray<uint8> Actual;
        if (!Test.TestTrue(TEXT("Original cached geometry is unavailable before cold LoadObject"),
                GetDerivedDataCacheRef().GetSynchronous(*Key, Actual, TEXT("P4OriginalParticleColdSourceProof")) &&
                    Actual == Wrong))
            return false;
    }
    for (int32 Index = First; Index < First + SourceCount; ++Index)
    {
        FSource Source;
        if (!Read(Test, Root, Index, Source))
            return false;
        if (!Test.TestNull(
                TEXT("Actual original package is unloaded in the cold process"), FindPackage(nullptr, *Source.Package)))
            return false;
        auto* System = LoadObject<UParticleSystem>(nullptr, *Source.ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        // No importer/factory or native tail application: ordinary engine reload must retain
        // game bools and restore exact source-bound original cutout geometry after stock PostLoad.
        if (!Check(Test, System, Source, Index))
            return false;
    }
    return !Test.HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalParticleSourceImportTest, "P4MapKit.Particles.ActualSourceImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalParticleSourceImportTest::RunTest(const FString&)
{
    return P4RunActualParticleSourceImport(*this, 0, 3, TEXT("cold-cache-keys.json"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalParticleSourceReloadTest, "P4MapKit.Particles.ActualSourceColdReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalParticleSourceReloadTest::RunTest(const FString&)
{
    return P4RunActualParticleSourceReload(*this, 0, 3, TEXT("cold-cache-keys.json"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalNpcParticleImportTest, "P4MapKit.Particles.ActualNpcSourceImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalNpcParticleImportTest::RunTest(const FString&)
{
    return P4RunActualParticleSourceImport(*this, 0, 2, TEXT("cold-npc-cache-keys.json"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalNpcParticleReloadTest, "P4MapKit.Particles.ActualNpcSourceColdReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalNpcParticleReloadTest::RunTest(const FString&)
{
    return P4RunActualParticleSourceReload(*this, 0, 2, TEXT("cold-npc-cache-keys.json"));
}

// The GPU emitter source whose editor curves the cook dropped. It imports as a real asset:
// its curves come back from their baked tables and its emitter resource data stays the game's.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalGpuParticleImportTest, "P4MapKit.Particles.ActualGpuSourceImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalGpuParticleImportTest::RunTest(const FString&)
{
    return P4RunActualParticleSourceImport(*this, 2, 1, TEXT("cold-gpu-cache-keys.json"));
}

#endif

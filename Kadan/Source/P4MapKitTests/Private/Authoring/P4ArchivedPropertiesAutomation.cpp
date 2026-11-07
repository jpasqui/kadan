#include "P4OriginalAssetImport.h"
#include "P4OriginalDecode.h"
#include "P4DecodedPropertyWriter.h"
#include "Animation/AnimSequence.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ArchivedPropertyStorageTest, "P4MapKit.Quality.ArchivedPropertyStorage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ArchivedPropertyStorageTest::RunTest(const FString&)
{
    const auto* Notifies = FindFProperty<FArrayProperty>(UAnimSequence::StaticClass(), TEXT("Notifies"));
    const auto* Event = Notifies ? CastField<FStructProperty>(Notifies->Inner) : nullptr;
    if (!TestNotNull(TEXT("Actual engine notify event storage"), Event))
        return false;
    if (!TestNull(TEXT("Stock engine cannot hold captured MiddleDuration"),
            Event->Struct->FindPropertyByName(TEXT("MiddleDuration"))))
        return false;
    P4MapKit::FDecodedValue Source, Bits;
    Bits.Fields.emplace_back(
        "__native_float32_bits", P4MapKit::FDecodedValue::Atom(P4MapKit::FDecodedValue::EKind::String, "4291082176"));
    Source.Fields.emplace_back("MiddleDuration", Bits); // Captured 0xffc4b7c0, not a generic "nan".
    FStructOnScope Storage(Event->Struct);
    FP4DecodedPropertyWriter Writer({}, true);
    Writer.Lenient = true;
    TestTrue(TEXT("Reference-only notify omission is diagnosed without manufacturing a finite default"),
        Writer.Value(Event, Storage.GetStructMemory(), Source, TEXT("Sequence Notifies[0]")));
    TestEqual(TEXT("Exactly one missing source field is reported"), Writer.NotHeld.Num(), 1);
    if (Writer.NotHeld.Num())
        TestEqual(TEXT("Missing value receipt preserves the entire captured NaN word"), Writer.NotHeld[0],
            FString(TEXT("Sequence Notifies[0].MiddleDuration = {\"__native_float32_bits\":\"4291082176\"}")));
    FP4DecodedPropertyWriter Strict({}, true);
    TestFalse(TEXT("Faithful notify import cannot silently omit the engine extension"),
        Strict.Value(Event, Storage.GetStructMemory(), Source, TEXT("Sequence Notifies[0]")));

    UStruct* Water = FindObject<UStruct>(nullptr, TEXT("/Script/Carrot4.WaterBoxExternalData"));
    std::vector<P4MapKit::FMapStructCodec> Codecs;
    if (!TestNotNull(TEXT("Recovered owning WaterBox schema is registered"), Water) ||
        !TestTrue(TEXT("Recovered map codecs are discoverable"), P4MapStructCodecs(Water, Codecs)))
        return false;
    const auto Found = std::find_if(Codecs.begin(), Codecs.end(),
        [](const P4MapKit::FMapStructCodec& C)
        {
            return C.Owner == "WaterBoxExternalData" && C.Property == "WaterColorList" &&
                C.Value == "WaterColorParameter" && !C.ValueNative;
        });
    TestTrue(TEXT("Water map uses captured tagged WaterColorParameter schema instead of guessing a byte layout"),
        Found != Codecs.end());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4WaterBoxOriginalImportTest, "P4MapKit.Original.WaterBoxImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4WaterBoxOriginalImportTest::RunTest(const FString&)
{
    const FString SourceRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ARCHIVED_SOURCE"));
    if (SourceRoot.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "WaterBoxImport not exercised: set P4_NATIVE_ARCHIVED_SOURCE to retained ConvertedOriginals/Carrot4/Content."));
        return true;
    }
    const FString Header = SourceRoot / TEXT("Carrot4/Placeables/Gimmicks/WaterBox/WBE_WaterBoxExternal.uasset");
    const FString PackagePath = TEXT("/Game/Carrot4/Placeables/Gimmicks/WaterBox/WBE_WaterBoxExternal");
    FString Target;
    if (!TestTrue(TEXT("Original target path resolves under this test project"),
            FPackageName::TryConvertLongPackageNameToFilename(PackagePath, Target, TEXT(".uasset"))))
        return false;
    if (!TestTrue(TEXT("Test target is free before source import"),
            !FindPackage(nullptr, *PackagePath) && !IFileManager::Get().FileExists(*Target)))
        return false;
    ON_SCOPE_EXIT
    {
        if (UPackage* Package = FindPackage(nullptr, *PackagePath))
        {
            if (UObject* Asset = FindObject<UObject>(Package, TEXT("WBE_WaterBoxExternal")))
            {
                FAssetRegistryModule::AssetDeleted(Asset);
                Asset->ClearFlags(RF_Public | RF_Standalone);
                Asset->MarkPendingKill();
            }
            ResetLoaders(Package);
            Package->SetDirtyFlag(false);
        }
        IFileManager::Get().Delete(*Target, false, true, true);
    };
    const auto Imported = P4ImportOriginalAssetNative(Header, FString(), true);
    if (!TestTrue(TEXT("Actual original WaterBox import, save/reload and diagnostic cook comparison pass"),
            Imported.IsValid()))
    {
        AddError(Imported.ErrorDetail);
        return false;
    }
    TestTrue(TEXT("Actual import was published and compared"),
        Imported.Created && Imported.Saved && Imported.ComparedKnown && Imported.Compared > 0);
    TestFalse(TEXT("Water colors have no omitted native bytes or source values"), Imported.ReferenceOnly);
    TestEqual(TEXT("No WaterBox field is silently dropped"), Imported.ValuesNotHeldCount, 0);
    UObject* Asset = Imported.Asset;
    const auto* Map = Asset ? FindFProperty<FMapProperty>(Asset->GetClass(), TEXT("WaterColorList")) : nullptr;
    if (!TestNotNull(TEXT("Imported real map property exists"), Map))
        return false;
    FScriptMapHelper Values(Map, Map->ContainerPtrToValuePtr<void>(Asset));
    TestEqual(TEXT("Both original enum/color/density map entries survive actual import"), Values.Num(), 2);
    return !HasAnyErrors();
}
#endif

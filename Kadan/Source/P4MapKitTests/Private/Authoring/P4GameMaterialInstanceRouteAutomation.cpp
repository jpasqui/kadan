#if WITH_DEV_AUTOMATION_TESTS
#include "P4ExportDriver.h"
#include "P4GameAssetFamilies.h"
#include "P4GameReplacement.h"
#include "P4KadanAssetPolicy.h"
#include "P4MaterialInstanceGameSettings.h"
#include "P4MaterialInstancePermutation.h"
#include "P4ModDescriptor.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4OriginalRecord.h"
#include "P4TranslucentMaterialStaging.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
template <typename T>
T* MakeAsset(const FString& Name)
{
    UPackage* Package = CreatePackage(*(TEXT("/Game/__KadanFamily/") + Name));
    return NewObject<T>(Package, FName(*Name), RF_Public | RF_Standalone);
}

// Recovered game assets are imported read-only, as these records say.
bool MarkOriginal(UObject* Asset, const TCHAR* Kind, const TCHAR* Schema, const TCHAR* DigestKind)
{
    FP4OriginalRecord Record;
    Record.Package = Asset->GetOutermost()->GetName();
    Record.Class = Asset->GetClass()->GetPathName();
    Record.Kind = Kind;
    Record.ComparisonSchema = Schema;
    Record.SourceDigestKind = DigestKind;
    Record.Fidelity = TEXT("recovered");
    Record.ReadOnly = true;
    FString Error;
    return P4MarkOriginalAsset(Asset, Record, Error);
}

void Discard(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    Package->SetDirtyFlag(false);
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    for (UObject* Object : Objects)
    {
        Object->ClearFlags(RF_Public | RF_Standalone);
        Object->SetFlags(RF_Transient);
    }
    Package->SetFlags(RF_Transient);
}
} // namespace

// The edited game instance route without the game: a synthetic recovered instance and parent stand in
// for an import. It checks the replacement report, export availability and what staging compiles.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameMaterialInstanceRoute, "P4MapKit.Kadan.GameMaterialInstanceRoute",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameMaterialInstanceRoute::RunTest(const FString&)
{
    using namespace P4Kadan;
    const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UMaterial* Master = MakeAsset<UMaterial>(TEXT("M_Route_") + Suffix);
    UMaterialInstanceConstant* Instance = MakeAsset<UMaterialInstanceConstant>(TEXT("MI_Route_") + Suffix);
    ON_SCOPE_EXIT
    {
        Discard(Instance);
        Discard(Master);
    };
    Instance->SetParentEditorOnly(Master);
    UP4MaterialInstanceGameSettings::FindOrAdd(Instance);
    if (!TestTrue(TEXT("Synthetic game master record"),
                  MarkOriginal(Master, TEXT("material"), TEXT(""), TEXT("material-graph-v2-md5"))) ||
        !TestTrue(TEXT("Synthetic game instance record"),
                  MarkOriginal(Instance, TEXT("material-instance"), TEXT("material-instance-native-state-v1"),
                               TEXT("material-instance-v2-md5"))))
    {
        return false;
    }
    TestTrue(TEXT("The synthetic instance reads as a recovered game instance"), IsRecoveredGameMaterialInstance(Instance));
    TestFalse(TEXT("An untouched game instance is not a replacement"), P4IsGameReplacement(Instance));
    const bool Enabled = GameAssetShipping(EGameAssetFamily::MaterialInstance) == EGameAssetShipping::WholePackage;
    P4RecordGameMaterialInstanceEdit(Instance);
    FP4GameReplacement Replacement;
    if (!TestEqual(TEXT("Edit intent admission follows the family row"), P4IsGameReplacement(Instance, &Replacement), Enabled) ||
        !Enabled)
    {
        return true;
    }
    TestEqual(TEXT("Replacement kind"), Replacement.Kind, FString(TEXT("edited-game-material-instance")));
    TestEqual(TEXT("Replacement class"), Replacement.Class, UMaterialInstanceConstant::StaticClass()->GetPathName());
    TestEqual(TEXT("Replacement keeps the game path"), Replacement.Package, Instance->GetOutermost()->GetName());

    FP4ModDescriptor Mod;
    Mod.Id = TEXT("mi.route");
    Mod.Name = TEXT("Route");
    Mod.Version = TEXT("1.0.0");
    // A parameter-only edit needs no map of its own: it is exportable on either shader route.
    Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Value")), 0.25f);
    TestFalse(TEXT("A parameter edit needs no own shader map"), P4MaterialInstanceNeedsOwnShaderMap(Instance));
    const FP4ExportAvailability Scalar = P4ExportAssetAvailability(Instance, Mod);
    TestTrue(TEXT("A parameter edit is exportable: ") + Scalar.Reason, Scalar.Route == EP4ExportAssetRoute::CreatorMaterialInstance);
    {
        FP4TranslucentMaterialStaging Staging;
        const TArray<UObject*> Assets{Instance};
        FString Error;
        const bool Prepared = Staging.Prepare(TEXT("/Game/Mods/mi_route"), Assets, Error);
        TestTrue(TEXT("Staging accepts the parameter edit: ") + Error, Prepared);
        TestFalse(TEXT("Staging compiles nothing for a parameter edit"), Staging.HasMaterials());
        TestFalse(TEXT("The game instance keeps the game master's shaders"), Staging.IsRuntimeAsset(Instance));
    }
    // A base override needs a map. Availability says what the export can do on this shader route.
    Instance->BasePropertyOverrides.bOverride_TwoSided = true;
    Instance->BasePropertyOverrides.TwoSided = true;
    TestTrue(TEXT("A base override needs an own shader map"), P4MaterialInstanceNeedsOwnShaderMap(Instance));
    const FP4ExportAvailability Structural = P4ExportAssetAvailability(Instance, Mod);
    if (P4UseSwitchShaderRoute())
    {
        TestTrue(TEXT("The Switch route exports a base override: ") + Structural.Reason,
                 Structural.Route == EP4ExportAssetRoute::CreatorMaterialInstance);
    }
    else
    {
        TestFalse(TEXT("Without the Switch route a base override is not exportable"), Structural.IsAvailable());
        TestTrue(TEXT("The refusal names the Switch shader compiler"), Structural.Reason.Contains(TEXT("Switch shader compiler")));
    }
    return true;
}
#endif

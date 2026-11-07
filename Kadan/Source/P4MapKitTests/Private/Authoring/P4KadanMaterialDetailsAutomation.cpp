#include "P4MaterialGameSettings.h"
#include "P4OriginalRecord.h"
#include "P4KadanAssetPolicy.h"
#include "IDetailsView.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PropertyEditorModule.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanMaterialDetails, "P4MapKit.Kadan.MaterialDetails",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKadanMaterialDetails::RunTest(const FString&)
{
    FProperty* Capsule = FindFProperty<FProperty>(UP4MaterialGameSettings::StaticClass(), TEXT("C4ShaderParam0"));
    FProperty* Unknown = FindFProperty<FProperty>(UP4MaterialGameSettings::StaticClass(), TEXT("C4ShaderParam3"));
    if (!TestNotNull(TEXT("The serialized known field is retained"), Capsule) ||
        !TestNotNull(TEXT("The serialized unknown field is retained"), Unknown))
    {
        return false;
    }
    TestEqual(TEXT("A known field has its friendly label"), Capsule->GetDisplayNameText().ToString(), FString(TEXT("Capsule shadow scale")));
    TestTrue(TEXT("An unknown field belongs in Advanced"), Unknown->HasAnyPropertyFlags(CPF_AdvancedDisplay));
    const auto* Clipping = FindFProperty<FProperty>(UP4MaterialGameSettings::StaticClass(), TEXT("C4MaterialParam3"));
    TestTrue(TEXT("The uncompiled value is read-only in generic asset user data too"),
        Clipping && Clipping->HasAnyPropertyFlags(CPF_EditConst));
    const FString Name = TEXT("/Game/Mods/MaterialDetailsTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(CreatePackage(*Name), TEXT("ProtectedFixture"), RF_Public | RF_Standalone));
    ON_SCOPE_EXIT
    {
        Material->GetOutermost()->SetDirtyFlag(false);
        Material->ClearFlags(RF_Public | RF_Standalone);
        Material->SetFlags(RF_Transient);
    };
    FP4OriginalRecord Record;
    Record.Package = Name;
    Record.Class = Material->GetClass()->GetPathName();
    Record.Kind = TEXT("material");
    Record.SourceDigest = FString::ChrN(64, TEXT('a'));
    Record.ComparisonDigest = FString::ChrN(64, TEXT('b'));
    Record.SourceDigestKind = TEXT("fixture");
    Record.ComparisonSchema = TEXT("fixture");
    FString Error;
    if (!TestTrue(TEXT("The owned fixture is marked as an original"), P4MarkOriginalAsset(Material.Get(), Record, Error)) ||
        !TestFalse(TEXT("The shared policy protects the material"), P4Kadan::GetAssetPolicy(Material.Get()).AllowEdit))
    {
        return false;
    }
    TestNull(TEXT("The fixture begins without a settings object"), UP4MaterialGameSettings::Find(Material.Get()));
    Material->GetOutermost()->SetDirtyFlag(false);
    FDetailsViewArgs Args;
    Args.bUpdatesFromSelection = false;
    TSharedPtr<IDetailsView> Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
    Details->SetObject(Material.Get());
    TestNull(TEXT("Displaying protected Details does not create settings"), UP4MaterialGameSettings::Find(Material.Get()));
    TestFalse(TEXT("Displaying protected Details preserves package dirtiness"), Material->GetOutermost()->IsDirty());
    Details->SetObject(nullptr);
    Details.Reset();
    return !HasAnyErrors();
}
#endif

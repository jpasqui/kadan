#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialInstanceComparison.h"
#include "P4KadanImport.h"
#include "P4KadanImportSources.h"
#include "P4LevelContent.h"
#include "P4OriginalRecord.h"
#include "P4MaterialRecovery.h"
#include "P4MaterialGraph.h"
#include "P4NativeHash.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialInstanceBindingComparisonTest,
    "P4MapKit.Kadan.MaterialInstanceOriginalBindings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialInstanceBindingComparisonTest::RunTest(const FString&)
{
    // Existing saved warm original only: this gate never imports, saves, or
    // changes provenance. Missing fixture is a failed required gate, not a skip.
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_BINDING_SOURCE"));
    const FString Archive = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_BINDING_ARCHIVE"));
    const FString Package = TEXT("/Game/Carrot4/Effects/Cmn/Materials/MI_eft_Cmn_Awa00_alcfr");
    const FString ParentPackage = TEXT("/Game/Carrot4/MasterMaterial/eft/M_eft_Water01_alcfr_Master");
    if (!TestFalse(TEXT("Exact raw MIC source root is required"), Root.IsEmpty()))
        return false;
    if (!TestFalse(
            TEXT("Exact existing game shader archive is required for a read-only recovery witness"), Archive.IsEmpty()))
        return false;
    auto* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *(Package + TEXT(".MI_eft_Cmn_Awa00_alcfr")));
    auto* Parent = Instance ? Cast<UMaterial>(Instance->Parent) : nullptr;
    if (!TestTrue(TEXT("Existing original MIC and direct recovered parent are present"),
            Instance && Parent && P4ExactNativeString(Parent->GetOutermost()->GetName(), ParentPackage) &&
                Instance->ScalarParameterValues.Num() > 1))
        return false;
    FP4OriginalRecord Record;
    FString Error;
    if (!TestTrue(TEXT("Existing original retains the recorded source/cook baseline"),
            P4ReadOriginalRecord(Instance, Record, Error) && Record.ComparisonSchema == TEXT("object-lines-v1") &&
                Record.ComparisonDigest == TEXT("7110a081833b600ab18acee63fb926e5aba12dabd2dc020b847ba8a626739506") &&
                Record.SourceDigest == TEXT("df2e2d822afc58f105bf174978708a43eb378bf153c9132993fe0926c17387c6")))
        return false;
    const FString Marker = Instance->GetOutermost()->GetMetaData()->GetValue(Instance, TEXT("P4OriginalRecord"));
    const bool WasDirty = Instance->GetOutermost()->IsDirty(), ParentWasDirty = Parent->GetOutermost()->IsDirty();
    const auto Scalars = Instance->ScalarParameterValues;
    const bool TwoSided = Parent->TwoSided;
    ON_SCOPE_EXIT
    {
        Instance->ScalarParameterValues = Scalars;
        Parent->TwoSided = TwoSided;
        Instance->GetOutermost()->SetDirtyFlag(WasDirty);
        Parent->GetOutermost()->SetDirtyFlag(ParentWasDirty);
    };
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    for (const FString& P : {Package, ParentPackage})
    {
        auto Source = MakeShared<FP4KadanSourceAsset>();
        Source->Package = P;
        Source->Kind = P == Package ? TEXT("MaterialInstanceConstant") : TEXT("Material");
        Source->Header = Root / TEXT("Carrot4/Content") / (P.Mid(6) + TEXT(".uasset"));
        Source->Body = FPaths::ChangeExtension(Source->Header, TEXT("uexp"));
        Sources.Add(Source);
    }
    TArray<FString> RawLines;
    const FString Strict = P4OriginalObjectAssetStateAndLines(Instance, RawLines);
    if (!TestTrue(TEXT("Without a source scope, strict original digest remains different"),
            P4ExactNativeString(Strict, TEXT("b32dda39b8920945725cbc9f661cbedd2e17b80af69f05d508df64101d64b46d"))))
        return false;
    {
        FP4KadanImportSourceScope PartialScope(Sources);
        auto Held = RawLines;
        TestTrue(TEXT("Raw parent identity alone supplies no historical recipe/read witness"),
            !P4ProjectOriginalMaterialInstanceBindings(Instance, Held, Error) && Held == RawLines && !Error.IsEmpty());
    }
    // Establish an actual read-only complete recovery witness. It constructs no
    // asset and cannot recapture the existing parent's native or recipe record.
    TArray<TSharedPtr<FP4KadanSourceAsset>> AllSources;
    TArray<FString> Problems;
    if (!TestTrue(TEXT("Existing extracted source universe scans"),
            P4KadanScanGame(Root, AllSources, Problems) && !Problems.Num()))
        return false;
    FP4KadanImportSourceScope Scope(AllSources);
    FP4MaterialRecovery Recovery(Root, AllSources, Archive);
    const auto Recovered = Recovery.Recover(ParentPackage);
    if (!TestTrue(TEXT("Actual parent family recovers without import/save: ") + Recovered.Reason,
            Recovered.bOk && Recovered.Kind == TEXT("Material")))
        return false;
    FP4KadanSourceBinding ParentBinding;
    if (!TestTrue(TEXT("Current exact parent bytes and saved recipe are linked by actual recovery reads: ") + Error,
            P4KadanReadBoundSource(ParentPackage, ParentBinding, Error) &&
                P4CurrentRecoveredMaterialParentRecipe(
                    ParentPackage, ParentBinding.SourceDigest, FMD5::HashAnsiString(*Recovered.Source), Error)))
        return false;
    TestFalse(TEXT("Another recipe identity cannot reuse the existing recovery witness"),
        P4CurrentRecoveredMaterialParentRecipe(
            ParentPackage, ParentBinding.SourceDigest, TEXT("00000000000000000000000000000000"), Error));
    const FString ParentNativeBefore = P4MaterialComparisonState(Parent, Error);
    if (!TestFalse(TEXT("Existing recovered parent native state is readable"), ParentNativeBefore.IsEmpty()))
        return false;
    auto Projected = RawLines;
    if (!TestTrue(TEXT("Source-bound native parent frame projects existing GUID-only drift: ") + Error,
            P4ProjectOriginalMaterialInstanceBindings(Instance, Projected, Error) && Error.IsEmpty() &&
                Projected != RawLines))
        return false;
    if (!TestTrue(TEXT("ObjectState hook reproduces the exact unchanged recorded baseline"),
            P4DiagnoseOriginalAsset(Instance).Comparison == EP4OriginalComparison::Equal))
        return false;
    const auto Different = [&](const TCHAR* Label)
    {
        // Each negative re-cooks current values through the same production
        // hook; no stale supplied lines can stand in for a native content edit.
        const auto Diagnostic = P4DiagnoseOriginalAsset(Instance);
        return TestTrue(Label,
            Diagnostic.Comparison == EP4OriginalComparison::Different ||
                Diagnostic.Comparison == EP4OriginalComparison::Unreadable);
    };
    Instance->ScalarParameterValues[0].ExpressionGUID = FGuid(1, 2, 3, 4);
    Different(TEXT("A GUID outside current recovered-parent binding stays different"));
    Instance->ScalarParameterValues = Scalars;
    Instance->ScalarParameterValues[0].ParameterValue += 0.125f;
    Different(TEXT("A scalar edit stays different"));
    Instance->ScalarParameterValues = Scalars;
    Instance->ScalarParameterValues[0].ParameterInfo.Name = FName(TEXT("CreatorChangedParameter"));
    Different(TEXT("A parameter-name edit stays different"));
    Instance->ScalarParameterValues = Scalars;
    Instance->ScalarParameterValues[0].ParameterInfo.Association = EMaterialParameterAssociation::LayerParameter;
    Different(TEXT("A parameter-association edit stays different"));
    Instance->ScalarParameterValues = Scalars;
    Instance->ScalarParameterValues[0].ParameterInfo.Index = 0;
    Different(TEXT("A parameter-layer edit stays different"));
    Instance->ScalarParameterValues = Scalars;
    Parent->TwoSided = !TwoSided;
    Different(TEXT("A changed parent graph cannot supply trusted binding GUIDs"));
    Parent->TwoSided = TwoSided;
    {
        auto Wrong = MakeShared<FP4KadanSourceAsset>(*Sources[0]);
        Wrong->Header = Sources[1]->Header;
        Wrong->Body = Sources[1]->Body;
        FP4KadanImportSourceScope WrongSource({Wrong, Sources[1]});
        Different(TEXT("A bound source with another package identity stays different"));
    }
    {
        const FString Folder = FPaths::ProjectSavedDir() / TEXT("Kadan/MaterialBindingCompareControls") /
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString Header = Folder / TEXT("Parent.uasset"), Body = Folder / TEXT("Parent.uexp");
        if (!TestFalse(TEXT("Fresh exact owned control path is unused"), IFileManager::Get().DirectoryExists(*Folder)))
            return false;
        IFileManager::Get().MakeDirectory(*Folder, true);
        ON_SCOPE_EXIT
        {
            IFileManager::Get().Delete(*Header, false, true);
            IFileManager::Get().Delete(*Body, false, true);
            IFileManager::Get().DeleteDirectory(*Folder, false, false);
        };
        auto ChangedBody = ParentBinding.Body;
        if (!TestTrue(TEXT("Parent body control has bytes"), ChangedBody.Num() > 0))
            return false;
        ChangedBody.Last() ^= 1;
        if (!TestTrue(TEXT("Only private two-file source control is written"),
                FFileHelper::SaveArrayToFile(ParentBinding.Header, *Header) &&
                    FFileHelper::SaveArrayToFile(ChangedBody, *Body)))
            return false;
        TArray<TSharedPtr<FP4KadanSourceAsset>> Shadow;
        for (const auto& Source : AllSources)
            if (Source->Package != ParentPackage)
                Shadow.Add(Source);
        auto Changed = MakeShared<FP4KadanSourceAsset>();
        Changed->Package = ParentPackage;
        Changed->Kind = TEXT("Material");
        Changed->Header = Header;
        Changed->Body = Body;
        Shadow.Add(Changed);
        FP4KadanImportSourceScope ChangedParentScope(Shadow);
        FP4KadanSourceBinding ChangedBinding;
        if (!TestTrue(TEXT("Changed raw bytes still have the same canonical parent package identity"),
                P4KadanReadBoundSource(ParentPackage, ChangedBinding, Error) &&
                    !P4ExactNativeString(ChangedBinding.SourceDigest, ParentBinding.SourceDigest)))
            return false;
        TestTrue(TEXT("Same-package raw source control leaves the live recovered parent native state unchanged"),
            P4ExactNativeString(P4MaterialComparisonState(Parent, Error), ParentNativeBefore));
        Different(TEXT("Same canonical parent with changed raw bytes cannot reuse the recovery witness"));
    }
    Instance->ScalarParameterValues = Scalars;
    TestTrue(TEXT("Restoring actual values restores exact existing baseline without recapture"),
        P4DiagnoseOriginalAsset(Instance).Comparison == EP4OriginalComparison::Equal);
    TestTrue(TEXT("Comparison did not rewrite original custody or package dirty flags"),
        P4ExactNativeString(
            Marker, Instance->GetOutermost()->GetMetaData()->GetValue(Instance, TEXT("P4OriginalRecord"))) &&
            Instance->GetOutermost()->IsDirty() == WasDirty && Parent->GetOutermost()->IsDirty() == ParentWasDirty);
    return !HasAnyErrors();
}
#endif

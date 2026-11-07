#include "P4KadanEditor.h"
#include "P4KadanReadOnly.h"
#include "P4OriginalRecord.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// An edit of a locked game material confined to a later element of a fixed array (UMaterial::CustomizedUVs[8]) is put
// back like any other, and the package is left dirty only when something stayed changed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanReadOnlyFixedArrayTest, "P4MapKit.Kadan.ReadOnlyFixedArray",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanReadOnlyFixedArrayTest::RunTest(const FString&)
{
    P4RegisterKadanEditor();
    const FString Name = TEXT("/Game/__KadanFixedArray_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TStrongObjectPtr<UPackage> Package(CreatePackage(*Name));
    TStrongObjectPtr<UMaterial> Asset(
        NewObject<UMaterial>(Package.Get(), TEXT("Protected"), RF_Public | RF_Standalone | RF_Transactional));
    ON_SCOPE_EXIT
    {
        Package->GetMetaData()->RemoveValue(Asset.Get(), TEXT("P4OriginalRecord"));
        P4Kadan::UpdateReadOnlyAsset(Asset.Get());
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->SetFlags(RF_Transient);
        Package->SetDirtyFlag(false);
        Package->SetFlags(RF_Transient);
    };
    FP4OriginalRecord Record;
    Record.Package = Name;
    Record.Class = Asset->GetClass()->GetPathName();
    Record.Kind = TEXT("test-game-asset");
    Record.SourceDigest = FString::ChrN(64, TEXT('a'));
    Record.ComparisonDigest = FString::ChrN(64, TEXT('b'));
    Record.SourceDigestKind = TEXT("fixture");
    Record.ComparisonSchema = TEXT("fixture");
    FString Why;
    if (!TestTrue(TEXT("The material is locked"),
            P4MarkOriginalAsset(Asset.Get(), Record, Why) && P4Kadan::IsLockedObject(Asset.Get())))
        return false;
    FCoreUObjectDelegates::OnObjectModified.Broadcast(Asset.Get());
    Asset->CustomizedUVs[3].UseConstant = true;
    Asset->CustomizedUVs[3].Constant = FVector2D(5.f, 7.f);
    P4Kadan::FlushReadOnlyGuards();
    TestFalse(TEXT("The edit of the fourth customized UV is put back"), bool(Asset->CustomizedUVs[3].UseConstant));
    TestEqual(TEXT("The fourth customized UV's constant is put back"), Asset->CustomizedUVs[3].Constant,
        FVector2D::ZeroVector);
    TestTrue(TEXT("Kadan says it put the edit back"),
        P4Kadan::TakeReadOnlyMessages().ContainsByPredicate(
            [](const FString& Message) { return Message.Contains(TEXT("put back")); }));
    return !HasAnyErrors();
}

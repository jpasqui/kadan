#if WITH_DEV_AUTOMATION_TESTS
#include "P4KadanExecutionEdges.h"
#include "P4KadanTestSeams.h"
#include "P4KadanImport.h"
#include "P4KadanAcquisitionVeto.h"
#include "P4NativeHash.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialParameterCollection.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

namespace
{
FP4MaterialUsedResources MaterialHydrationControl(bool Available)
{
    const FString Hash = TEXT("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    FP4MaterialUsedResources Out;
    Out.Status = EP4MaterialUsedResourceStatus::Complete;
    Out.Master = TEXT("/Game/MaterialExecutionControls/Master");
    Out.FamilySourceSetSha256 = Hash;
    Out.RegistryLeaseEpoch = Hash;
    Out.ArchiveSha256 = Hash;
    FP4MaterialResourceOwnerProof Owner;
    Owner.Owner = Out.Master;
    Owner.OwnerSourceSha256 = Hash;
    Owner.ResourceHash = TEXT("actual-program-resource");
    Owner.GraphSha256 = Available ? FString::ChrN(64, TCHAR('b')) : FString::ChrN(64, TCHAR('a'));
    FP4MaterialResourceProgram Program;
    Program.ArchiveIndex = 7;
    Program.Frequency = 3;
    Program.PayloadSha256 = Hash;
    Owner.Programs.Add(Program);
    FP4MaterialResourceSource Source;
    Source.Package = Owner.Owner;
    Source.HeaderSha256 = Hash;
    Source.BodySha256 = Hash;
    Source.SourceSha256 = Hash;
    Owner.Sources.Add(Source);
    Out.Owners.Add(Owner);
    FP4MaterialUsedResourceRequest Request;
    Request.Package = TEXT("/Game/MaterialExecutionControls/Texture");
    Request.Object = Request.Package + TEXT(".Texture");
    Request.NativeClass = TEXT("/Script/Engine.Texture2D");
    Request.bSourceAvailable = Available;
    if (Available)
    {
        Request.SourceSha256 = Hash;
        Request.SamplerType = TEXT("LinearColor");
    }
    FP4MaterialResourceOwnerUse Use;
    Use.Owner = Owner.Owner;
    Use.OwnerSourceSha256 = Hash;
    Use.ResourceHash = Owner.ResourceHash;
    Use.GraphSha256 = Owner.GraphSha256;
    FP4MaterialResourceConsumer Consumer;
    Consumer.Node = TEXT("TextureSampleParameter2D_0");
    Consumer.NodeType = TEXT("TextureSampleParameter2D");
    Consumer.Parameter = TEXT("Mask");
    Use.Consumers.Add(Consumer);
    FP4MaterialResourceReference Reference;
    Reference.SourcePackage = Owner.Owner;
    Reference.SourceSha256 = Hash;
    Reference.Property = TEXT("TextureParameterValues[0]");
    Reference.Object = Request.Object;
    Reference.NativeClass = Request.NativeClass;
    Reference.ImportIndex = -3;
    Use.References.Add(Reference);
    Request.Owners.Add(Use);
    Out.Requests.Add(Request);
    return Out;
}
TArray<uint8> MaterialControlObjectBytes(UObject* Object)
{
    TArray<uint8> Bytes;
    FObjectWriter Writer(Object, Bytes, false, false, false);
    return Bytes;
}
bool MaterialPlanContains(const FP4KadanImportPlan& Plan, const FString& Package)
{
    return Plan.Assets.ContainsByPredicate([&](const TSharedPtr<FP4KadanSourceAsset>& Row)
        { return Row.IsValid() && P4ExactNativeString(Row->Package, Package); });
}
} //namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialExecutionPolicyTest, "P4MapKit.Kadan.MaterialExecutionPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialExecutionPolicyTest::RunTest(const FString&)
{
    FString Error;
    const auto Before = MaterialHydrationControl(false), After = MaterialHydrationControl(true);
    TestTrue(TEXT("Actual new sampler metadata may change graph witness while source/program/uses stay exact"),
        P4KadanMaterialDiscoveryAfterHydration(Before, After, Error));
    auto Changed = After;
    Changed.Owners[0].Programs[0].PayloadSha256[0] = TCHAR('f');
    TestFalse(TEXT("Changed shader payload refuses"), P4KadanMaterialDiscoveryAfterHydration(Before, Changed, Error));
    Changed = After;
    Changed.Requests[0].Owners[0].GraphSha256 = Before.Owners[0].GraphSha256;
    TestFalse(TEXT("Inconsistent use/top-level owner graph linkage refuses"),
        P4KadanMaterialDiscoveryAfterHydration(Before, Changed, Error));
    Changed = After;
    const auto DuplicateOwner = Changed.Requests[0].Owners[0];
    Changed.Requests[0].Owners.Add(DuplicateOwner);
    TestFalse(
        TEXT("Duplicate request owner proof refuses"), P4KadanMaterialDiscoveryAfterHydration(Before, Changed, Error));
    Changed = After;
    Changed.Requests[0].Owners[0].References[0].ImportIndex = -4;
    TestFalse(
        TEXT("Changed original hard import refuses"), P4KadanMaterialDiscoveryAfterHydration(Before, Changed, Error));
    Changed = After;
    Changed.Requests[0].bSourceAvailable = false;
    TestFalse(TEXT("Unhydrated resource cannot become final admission"),
        P4KadanMaterialDiscoveryAfterHydration(Before, Changed, Error));
    Changed = After;
    Changed.Owners[0].Sources[0].SourceSha256[0] = TCHAR('f');
    TestFalse(TEXT("Changed old owner source refuses"), P4KadanMaterialDiscoveryAfterHydration(Before, Changed, Error));
    Changed = After;
    Changed.Requests[0].NativeClass = TEXT("/Script/Engine.TextureCube");
    TestFalse(
        TEXT("Changed typed resource class refuses"), P4KadanMaterialDiscoveryAfterHydration(Before, Changed, Error));
    auto Request = After.Requests[0];
    TestTrue(TEXT("Real native plane texture source route is executable"),
        P4KadanMaterialResourceCanExecute(Request, Error));
    Request.NativeClass = TEXT("/Script/Engine.VolumeTexture");
    TestTrue(TEXT("Real native volume texture source route is executable"),
        P4KadanMaterialResourceCanExecute(Request, Error));
    Request.NativeClass = TEXT("/Script/MediaAssets.MediaTexture");
    TestFalse(TEXT("Unsupported media native source does not get silently imported"),
        P4KadanMaterialResourceCanExecute(Request, Error));
    Request.Kind = EP4MaterialUsedResourceKind::Collection;
    Request.NativeClass = TEXT("/Script/Engine.MaterialParameterCollection");
    TestTrue(TEXT("Stock native collection has a typed source/value route, no cook polling"),
        P4KadanMaterialResourceCanExecute(Request, Error));
    Request.NativeClass = TEXT("/Script/Engine.Material");
    TestFalse(
        TEXT("Collection kind cannot promote another native class"), P4KadanMaterialResourceCanExecute(Request, Error));

    // Deliberately sourceless graph controls exercise selection/refusal only.
    // They never invoke RunImport and cannot be used as native source proof.
    const FString GraphRoot = TEXT("/Game/__P4MaterialExecutionGraph/") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto Master = MakeShared<FP4KadanSourceAsset>();
    Master->Package = GraphRoot / TEXT("Master");
    Master->Kind = TEXT("Material");
    auto Function = MakeShared<FP4KadanSourceAsset>();
    Function->Package = GraphRoot / TEXT("Function");
    Function->Kind = TEXT("MaterialFunction");
    auto Mesh = MakeShared<FP4KadanSourceAsset>();
    Mesh->Package = GraphRoot / TEXT("Mesh");
    Mesh->Kind = TEXT("StaticMesh");
    Master->Dependencies.Add(Function->Package);
    Mesh->Dependencies.Add(Function->Package);
    const TArray<TSharedPtr<FP4KadanSourceAsset>> Rows{Master, Function, Mesh};
    const auto Legacy = P4KadanPlanImport(Rows, {Master->Package}, false);
    const auto ExplicitFalse = P4KadanPlanImport(Rows, {Master->Package}, false, false);
    TestTrue(TEXT("Default legacy/no-container array path retains raw dependencies"),
        MaterialPlanContains(Legacy, Function->Package) && Legacy.Assets.Num() == ExplicitFalse.Assets.Num());
    const auto ExplicitRoot = P4KadanPlanImport(Rows, {Function->Package}, false, true);
    TestTrue(TEXT("Independently requested function is retained, never pruned"),
        MaterialPlanContains(ExplicitRoot, Function->Package));
    TestTrue(TEXT("Sourceless requested function correctly refuses execution"), ExplicitRoot.Problems.Num() > 0);
    const auto Shared = P4KadanPlanImport(Rows, {Mesh->Package}, false, true);
    TestTrue(TEXT("Non-material hard requirement still executes its shared function"),
        MaterialPlanContains(Shared, Function->Package));
    TestTrue(TEXT("Sourceless graph control cannot pass source admission"), Shared.Problems.Num() > 0);
    const auto Absent = P4KadanPlanImport(Rows, {GraphRoot / TEXT("Absent")}, false, true);
    TestTrue(TEXT("Absent requested source explains why identity could not be verified"),
        Absent.Problems.ContainsByPredicate(
            [](const FString& Why) { return Why.Contains(TEXT("No scanned source has this package identity.")); }));

    // An occupied creator texture is preserved BEFORE touching source inputs or
    // reaching native import. This is independent of caller Resume preference.
    const FString Package = TEXT("/Game/__P4MaterialExecutionControls/Creator_") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Owner = CreatePackage(*Package);
    UTexture2D* Creator = NewObject<UTexture2D>(
        Owner, FName(*FPackageName::GetShortName(Package)), RF_Public | RF_Standalone);
    ON_SCOPE_EXIT
    {
        Creator->ClearFlags(RF_Public | RF_Standalone);
        Creator->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
        Creator->MarkPendingKill();
        Owner->SetDirtyFlag(false);
    };
    Owner->GetMetaData();
    Owner->SetDirtyFlag(false);
    const auto Bytes = MaterialControlObjectBytes(Creator);
    Request = After.Requests[0];
    Request.Package = Package;
    Request.Object = Creator->GetPathName();
    FP4KadanSourceAsset Source;
    Source.Package = Package;
    Source.Kind = TEXT("Texture2D");
    Source.Header = TEXT("must-not-read-a-creator-source.uasset");
    Source.Body = TEXT("must-not-read-a-creator-source.uexp");
    bool NeedsImport = true;
    TestFalse(TEXT("Occupied creator texture never authorizes native replacement"),
        P4KadanMaterialResourceNeedsImport(Request, Source, NeedsImport, Error));
    TestTrue(TEXT("Occupied refusal is the creator destination preflight"),
        Error.Contains(TEXT("occupied without an eligible")) && !NeedsImport);
    TestTrue(TEXT("Occupied texture diagnosis names exact resource and custody reason"),
        Error.Contains(Request.Package) && Error.Contains(Request.Object) &&
            Error.Contains(TEXT("original-record-unavailable")) && Error.Contains(TEXT("loaded_object=1")));
    TestTrue(TEXT("Creator bytes/dirty state/absence of original record unchanged"),
        MaterialControlObjectBytes(Creator) == Bytes && !Owner->IsDirty() &&
            !Owner->GetMetaData()->HasValue(Creator, TEXT("P4OriginalRecord")));
    {
        const FString CollectionPackage = Package + TEXT("_Collection");
        UPackage* CollectionOwner = CreatePackage(*CollectionPackage);
        auto* Collection = NewObject<UMaterialParameterCollection>(
            CollectionOwner, FName(*FPackageName::GetShortName(CollectionPackage)), RF_Public | RF_Standalone);
        ON_SCOPE_EXIT
        {
            Collection->ClearFlags(RF_Public | RF_Standalone);
            Collection->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
            Collection->MarkPendingKill();
            CollectionOwner->SetDirtyFlag(false);
        };
        CollectionOwner->GetMetaData();
        CollectionOwner->SetDirtyFlag(false);
        const auto CollectionBytes = MaterialControlObjectBytes(Collection);
        Request.Kind = EP4MaterialUsedResourceKind::Collection;
        Request.NativeClass = TEXT("/Script/Engine.MaterialParameterCollection");
        Request.Package = CollectionPackage;
        Request.Object = Collection->GetPathName();
        Source.Package = CollectionPackage;
        Source.Kind = TEXT("MaterialParameterCollection");
        NeedsImport = true;
        TestFalse(TEXT("Occupied creator collection refuses before reading absent source or importing"),
            P4KadanMaterialResourceNeedsImport(Request, Source, NeedsImport, Error));
        TestTrue(TEXT("Creator collection refuses at ownership boundary"),
            Error.Contains(TEXT("occupied without an eligible")) && !NeedsImport);
        TestTrue(TEXT("Occupied collection diagnosis names exact resource and custody reason"),
            Error.Contains(Request.Package) && Error.Contains(Request.Object) &&
                Error.Contains(TEXT("original-record-unavailable")) && Error.Contains(TEXT("loaded_object=1")));
        TestTrue(TEXT("Creator collection native bytes and absent provenance remain untouched"),
            CollectionBytes == MaterialControlObjectBytes(Collection) && !CollectionOwner->IsDirty() &&
                !CollectionOwner->GetMetaData()->HasValue(Collection, TEXT("P4OriginalRecord")));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualMaterialExecutionPlanTest, "P4MapKit.Kadan.ActualMaterialExecutionPlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualMaterialExecutionPlanTest::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_EXECUTION_SOURCE")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Opt in with the already retained exact material execution source inputs; no tools/archive/imports are used"));
        return true;
    }
    const FString Package = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_EXECUTION_PACKAGE"));
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_EXECUTION_HEADER"));
    const FString Body = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_EXECUTION_BODY"));
    TArray<TSharedPtr<FP4KadanSourceAsset>> Rows;
    TArray<FString> Problems;
    if (!TestTrue(TEXT("Actual bounded source folder scan"), P4KadanScanGame(FPaths::GetPath(Header), Rows, Problems)))
        return false;
    auto* Found = Rows.FindByPredicate(
        [&](const TSharedPtr<FP4KadanSourceAsset>& Row)
        {
            return Row->Package.Equals(Package, ESearchCase::CaseSensitive) &&
                FPaths::IsSamePath(Row->Header, Header) && FPaths::IsSamePath(Row->Body, Body);
        });
    if (!TestNotNull(TEXT("Exact opted-in actual row exists in scanner output"), Found))
        return false;
    const auto Source = *Found;
    const auto Raw = Source->Dependencies, Soft = Source->SoftDependencies;
    FString Error, BeforeDigest;
    if (!TestTrue(TEXT("Actual source canonical identity is current"),
            P4KadanRequestedSourceDigest(*Source, BeforeDigest, Error)))
    {
        AddError(Error);
        return false;
    }
    FP4KadanImportSourceScope Scope(Rows);
    FP4KadanExecutionEdges Edges;
    if (!TestTrue(TEXT("Actual Material/MIC execution witness"), Edges.Read(*Source, Error)))
    {
        AddError(Error);
        return false;
    }
    TArray<FString> Required;
    TestTrue(TEXT("Verified source role produces effective execution requirements"),
        Edges.Has(Package) && Edges.Dependencies(*Source, Required, Error));
    if (Source->Kind == TEXT("Material"))
    {
        FP4KadanSourceBinding Binding;
        FP4MaterialExecutionSource Bootstrap;
        if (!TestTrue(TEXT("Actual master typed prerequisites can be read"),
                P4KadanReadBoundSource(Package, Binding, Error) &&
                    P4ReadMaterialExecutionSource(Binding, Bootstrap, Error)))
            return false;
        TestEqual(
            TEXT("Only witnessed prerequisites join master execution"), Required.Num(), Bootstrap.Resources.Num());
        for (const auto& Edge : Bootstrap.Resources)
            TestTrue(TEXT("Master prerequisites are typed native collections, never raw functions/textures"),
                Edge.Kind == EP4MaterialExecutionResourceKind::ParameterCollection && Required.Contains(Edge.Package));
    }
    else
        TestTrue(TEXT("Actual MIC retains structural Parent and authored texture execution"), Required.Num() > 0);
    const auto Typed = P4KadanPlanImport(Rows, {Package}, true, true);
    TestTrue(TEXT("Actual settings-enabled planner retains exact requested root"),
        Typed.Problems.Num() == 0 && MaterialPlanContains(Typed, Package) && Typed.RequestedPackages.Num() == 1 &&
            Typed.RequestedPackages[0].Equals(Package, ESearchCase::CaseSensitive));
    const FString SelectorAlias = FString(TEXT("/Game/")) + Package.Mid(6).ToUpper();
    const auto Aliased = P4KadanPlanImport(Rows, {SelectorAlias}, true, true);
    TestTrue(TEXT("A case-varied selector resolves to the actual header spelling and source digest"),
        Aliased.Problems.Num() == 0 && Aliased.RequestedPackages == Typed.RequestedPackages &&
            Aliased.RequestedSourceDigests.FindRef(FName(*Package)) == BeforeDigest &&
            MaterialPlanContains(Aliased, Package));
    if (Source->Kind == TEXT("Material"))
        for (const auto& Row : Typed.Assets)
            TestTrue(TEXT("Unselected reverse MICs and raw function/default dependencies remain metadata"),
                Row->Package == Package || Row->Kind == TEXT("MaterialParameterCollection"));
    const FString PriorKind = Source->Kind, PriorHeader = Source->Header, PriorBody = Source->Body,
                  PriorPackage = Source->Package;
    Source->Kind = TEXT("Texture2D");
    TestFalse(TEXT("Changed shared descriptor kind refuses cached Read"), Edges.Read(*Source, Error));
    TestFalse(TEXT("Changed descriptor kind cannot consume stale execution edges"),
        Edges.Dependencies(*Source, Required, Error));
    Source->Kind = PriorKind;
    Source->Header += TEXT("_Changed");
    TestFalse(TEXT("Changed header descriptor refuses"), Edges.Dependencies(*Source, Required, Error));
    Source->Header = PriorHeader;
    Source->Body += TEXT("_Changed");
    TestFalse(TEXT("Changed body descriptor refuses"), Edges.Dependencies(*Source, Required, Error));
    Source->Body = PriorBody;
    Source->Package += TEXT("_Changed");
    TestFalse(TEXT("Changed package descriptor refuses original pointer witness"),
        Edges.Dependencies(*Source, Required, Error));
    Source->Package = PriorPackage;
    Source->Dependencies.Add(TEXT("/Game/MaterialExecutionControls/ChangedInventory"));
    TestFalse(TEXT("Changed raw inventory cannot consume prior proof"), Edges.Dependencies(*Source, Required, Error));
    Source->Dependencies = Raw;
    TestTrue(TEXT("Original actual descriptor and source are still current"),
        Edges.Read(*Source, Error) && Edges.Current(Error));
    FP4KadanImportSettings Extracted;
    Extracted.GameFolder = FPaths::GetPath(Header);
    const auto Old = P4KadanPlanImport(Rows, {Package}, true, false),
               NoPaks = P4KadanPlanImport(Extracted, {Package}, true);
    TestTrue(TEXT("No-Paks settings path exactly retains old array behavior"),
        !NoPaks.bMaterialExecutionDeferred && NoPaks.Assets.Num() == Old.Assets.Num() &&
            NoPaks.Dependencies == Old.Dependencies && NoPaks.Problems == Old.Problems);
    FP4KadanImportSettings Denied;
    Denied.GamePaks = FPaths::ProjectSavedDir();
    FP4KadanScopedAcquisitionVeto Veto(FPaths::ProjectDir());
    auto Wrong = Typed;
    Wrong.RequestedPackages.Add(TEXT("/Game/MaterialExecutionControls/Unrequested"));
    const auto CountRefusal = P4KadanRunImport(Denied, Wrong, true);
    TestTrue(TEXT("Requested-root count mismatch refuses before execution/acquisition"),
        !CountRefusal.Ok && CountRefusal.Imported.Num() == 0 && Veto.DeniedBatches() == 0);
    Wrong = Typed;
    Wrong.RequestedSourceDigests.FindChecked(FName(*Package)) = FString::ChrN(64, TCHAR('0'));
    const auto DigestRefusal = P4KadanRunImport(Denied, Wrong, true);
    TestTrue(TEXT("Changed requested source snapshot refuses before execution/acquisition"),
        !DigestRefusal.Ok && DigestRefusal.Imported.Num() == 0 && Veto.DeniedBatches() == 0);
    FString AfterDigest;
    TestTrue(TEXT("Actual source bytes and raw dependencies remain exact after all controls"),
        P4KadanRequestedSourceDigest(*Source, AfterDigest, Error) && P4ExactNativeString(BeforeDigest, AfterDigest) &&
            Raw == Source->Dependencies && Soft == Source->SoftDependencies);
    return !HasAnyErrors();
}
#endif

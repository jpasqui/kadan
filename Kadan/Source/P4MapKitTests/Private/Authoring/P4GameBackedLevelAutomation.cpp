#if P4_MAPKIT_TESTS
#include "P4OriginalLevelImport.h"
#include "P4OriginalRecord.h"
#include "P4LevelChanges.h"
#include "P4KadanImport.h"
#include "P4ExportDriver.h"
#include "P4ExportProcess.h"
#include "P4NativeHash.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/PointLight.h"
#include "Components/LightComponent.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

namespace P4ExportDriverTest
{
bool Request(FAutomationTestBase&, const FString&, const FString&, FP4ExportModRequest&, FP4ModNames&);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameBackedLevelExportTest, "P4MapKit.Export.GameBackedLevelCustodyAndExport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameBackedLevelExportTest::RunTest(const FString&)
{
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_GAME_BACKED_LEVEL"));
    if (Header.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Game-backed export not exercised: select a retained actual persistent map .umap in P4_NATIVE_GAME_BACKED_LEVEL and use a project with those destinations free."));
        return true;
    }
    const FString SourceFolder = FPaths::GetPath(Header);
    const bool Reuse = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_GAME_BACKED_REUSE")) == TEXT("1");
    TArray<TSharedPtr<FP4KadanSourceAsset>> Scanned;
    TArray<FString> Problems;
    if (!TestTrue(TEXT("Actual retained map headers scan without copying game data"),
            P4KadanScanGame(SourceFolder, Scanned, Problems)))
        return false;
    if (!TestEqual(TEXT("Actual map fixture has no source-read errors"), Problems.Num(), 0))
        return false;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Layers;
    TSharedPtr<FP4KadanSourceAsset> Root;
    for (const auto& Asset : Scanned)
    {
        if (Asset->Kind != TEXT("World"))
            continue;
        Layers.Add(Asset);
        if (FPaths::IsSamePath(Asset->Header, Header))
            Root = Asset;
        if (!Reuse &&
            !TestFalse(TEXT("Each actual layer destination is free before this isolated gate: ") + Asset->Package,
                FPackageName::DoesPackageExist(Asset->Package) || FindPackage(nullptr, *Asset->Package)))
            return false;
    }
    if (!TestTrue(TEXT("The exact requested persistent map source was found"), Root.IsValid() && Layers.Num() > 0))
        return false;
    // Child sources are saved first so normal editor streaming and export inspect
    // real package identities. This private gate deliberately declines original
    // actor construction; it never claims those originals were reconstructed.
    Layers.Remove(Root);
    Layers.Add(Root);
    TMap<FString, UWorld*> Worlds;
    TMap<FString, FString> SourceDigests;
    for (const auto& Layer : Layers)
    {
        TArray<uint8> HeaderBytes, BodyBytes;
        FString Expected;
        if (!TestTrue(TEXT("Independent exact source buffers are readable: ") + Layer->Package,
                FFileHelper::LoadFileToArray(HeaderBytes, *Layer->Header) &&
                    FFileHelper::LoadFileToArray(BodyBytes, *Layer->Body) &&
                    P4HashSourceBuffers(
                        HeaderBytes.GetData(), HeaderBytes.Num(), BodyBytes.GetData(), BodyBytes.Num(), Expected)))
            return false;
        FP4OriginalLevelImportResult Imported;
        if (Reuse && FPackageName::DoesPackageExist(Layer->Package))
        {
            Imported.Asset = LoadObject<UWorld>(
                nullptr, *(Layer->Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Layer->Package)));
            TSet<FName> Omitted;
            FString Digest, Error;
            if (!TestTrue(TEXT("Explicit reuse requires verified, unchanged game-backed source custody"),
                    Imported.Asset && P4ReadGameBackedLevelCustody(Imported.Asset, Omitted, Digest, Error) &&
                        Digest == Expected && P4AnalyzeOriginalLevelChanges(Imported.Asset).Unchanged))
            {
                AddError(Error);
                return false;
            }
            Imported.GameBacked = true;
            Imported.Saved = true;
            Imported.SourceDigest = Digest;
        }
        else
            Imported = P4ImportGameBackedLevelNative(Layer->Header, Layer->Body, true, {},
                TEXT("Controlled validation of an unavailable original actor reconstruction"), false);
        if (!TestTrue(TEXT("Actual source-backed editor layer is saved or explicitly reused: ") + Layer->Package,
                Imported.IsValid() && (Imported.Created || Reuse) && Imported.Saved && Imported.GameBacked &&
                    Imported.Asset))
        {
            AddError(Imported.ErrorDetail);
            return false;
        }
        TestFalse(TEXT("A projection does not claim whole-source cook equivalence"), Imported.ComparedKnown);
        TestEqual(TEXT("Independent source hash equals the custody hash"), Imported.SourceDigest, Expected);
        Worlds.Add(Layer->Package, Imported.Asset);
        SourceDigests.Add(Layer->Package, Expected);
        TSet<FName> Omitted;
        FString Digest, Error;
        if (!TestTrue(TEXT("Saved projection has verified original-source custody"),
                P4ReadGameBackedLevelCustody(Imported.Asset, Omitted, Digest, Error) && Digest == Expected))
        {
            AddError(Error);
            return false;
        }
        const auto Diagnostic = P4DiagnoseOriginalAsset(Imported.Asset);
        if (!TestTrue(TEXT("Explicit projection comparison equals its editor baseline"),
                Diagnostic.Error.IsEmpty() && Diagnostic.Comparison == EP4OriginalComparison::Equal))
        {
            AddError(Diagnostic.Error);
            return false;
        }
        const auto Changes = P4AnalyzeOriginalLevelChanges(Imported.Asset);
        if (!TestTrue(TEXT("Omitted source actors never become removals"),
                Changes.IsValid() && Changes.Unchanged && Changes.Removed.Num() == 0))
        {
            AddError(Changes.ErrorDetail);
            return false;
        }
        const FString CustodyBeforeResume = Imported.Asset->GetOutermost()->GetMetaData()->GetValue(
            Imported.Asset, TEXT("P4GameBackedLevelCustody"));
        const auto Resumed = P4ResumeOriginalLevelNative(Imported.Asset, Layer->Header, Layer->Body);
        if (!TestTrue(TEXT("Source-verified resume reuses the exact saved world without reconstructing it"),
                Resumed.IsValid() && Resumed.Asset == Imported.Asset && !Resumed.Created && Resumed.GameBacked &&
                    CustodyBeforeResume ==
                        Imported.Asset->GetOutermost()->GetMetaData()->GetValue(
                            Imported.Asset, TEXT("P4GameBackedLevelCustody"))))
        {
            AddError(Resumed.ErrorDetail);
            return false;
        }
    }
    UWorld* EditedWorld = nullptr;
    FString Reserved;
    for (const auto& Layer : Layers)
    {
        TSet<FName> Omitted;
        FString Digest, Error;
        UWorld* World = Worlds.FindRef(Layer->Package);
        if (!P4ReadGameBackedLevelCustody(World, Omitted, Digest, Error))
        {
            AddError(Error);
            return false;
        }
        for (FName Name : Omitted)
            if (!FindObject<AActor>(World->PersistentLevel, *Name.ToString()))
            {
                EditedWorld = World;
                Reserved = Name.ToString();
                break;
            }
        if (EditedWorld)
            break;
    }
    if (!TestTrue(TEXT("Actual game source contains a withheld actor identity"), EditedWorld && !Reserved.IsEmpty()))
        return false;
    // A creator cannot reuse an omitted original's name to replace it through
    // the addition path. The original remains protected even though invisible.
    FActorSpawnParameters Spawn;
    Spawn.OverrideLevel = EditedWorld->PersistentLevel;
    Spawn.Name = FName(*Reserved);
    APointLight* Collision = EditedWorld->SpawnActor<APointLight>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Controlled native actor can exercise reserved identity refusal"), Collision))
        return false;
    const auto Conflicted = P4AnalyzeOriginalLevelChanges(EditedWorld);
    TestFalse(TEXT("Replacing an uneditable original actor identity is refused"), Conflicted.IsValid());
    EditedWorld->DestroyActor(Collision);
    if (!TestTrue(TEXT("Removing the rejected addition restores the projection"),
            P4AnalyzeOriginalLevelChanges(EditedWorld).Unchanged))
        return false;

    // Tamper only in-memory custody metadata, restore it verbatim immediately,
    // and verify export's public guard refuses a changed source identity.
    UPackage* Package = EditedWorld->GetOutermost();
    UMetaData* Meta = Package->GetMetaData();
    const FString Key = TEXT("P4GameBackedLevelCustody");
    const FString Before = Meta->GetValue(EditedWorld, *Key);
    const FString SourceDigest = SourceDigests.FindRef(Package->GetName());
    if (!TestTrue(TEXT("Custody contains the independently verified digest"), Before.Contains(SourceDigest)))
        return false;
    const bool WasDirty = Package->IsDirty();
    Meta->SetValue(EditedWorld, *Key, *Before.Replace(*SourceDigest, *FString::ChrN(64, TEXT('0'))));
    TSet<FName> Omitted;
    FString Digest, Error;
    const bool TamperedAccepted = P4ReadGameBackedLevelCustody(EditedWorld, Omitted, Digest, Error);
    Meta->SetValue(EditedWorld, *Key, *Before);
    Package->SetDirtyFlag(WasDirty);
    TestFalse(TEXT("A custody/source digest mismatch is refused"), TamperedAccepted);
    if (!TestTrue(TEXT("Exact metadata restoration restores source custody"),
            P4ReadGameBackedLevelCustody(EditedWorld, Omitted, Digest, Error)))
        return false;
    if (!TestTrue(
            TEXT("Custody explicitly withholds actual original actors"), Before.Contains(TEXT("\"editable\":false"))))
        return false;
    Meta->SetValue(EditedWorld, *Key, *Before.Replace(TEXT("\"editable\":false"), TEXT("\"editable\":true")));
    const bool ChangedRowsAccepted = P4ReadGameBackedLevelCustody(EditedWorld, Omitted, Digest, Error);
    Meta->SetValue(EditedWorld, *Key, *Before);
    Package->SetDirtyFlag(WasDirty);
    TestFalse(TEXT("Changing withheld actor rows cannot bypass original identity protection"), ChangedRowsAccepted);
    if (!TestTrue(TEXT("Exact actor-row restoration restores source custody"),
            P4ReadGameBackedLevelCustody(EditedWorld, Omitted, Digest, Error)))
        return false;

    Spawn.Name = TEXT("KadanGameBackedAddedLight");
    APointLight* Added = EditedWorld->SpawnActor<APointLight>(FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Supported new native actor can be added to the game-backed layer"), Added))
        return false;
    ON_SCOPE_EXIT
    {
        EditedWorld->DestroyActor(Added);
    };
    Added->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    const auto Changes = P4AnalyzeOriginalLevelChanges(EditedWorld);
    if (!TestTrue(TEXT("Only the explicit new actor is an addition; skipped originals are not deletions"),
            Changes.IsValid() && Changes.Added.Num() == 1 && Changes.Added.Contains(Added->GetName()) &&
                Changes.Removed.Num() == 0))
    {
        AddError(Changes.ErrorDetail);
        return false;
    }
    const auto* EditedSource = Layers.FindByPredicate(
        [&](const TSharedPtr<FP4KadanSourceAsset>& Layer) { return Worlds.FindRef(Layer->Package) == EditedWorld; });
    if (!TestTrue(TEXT("The edited layer retains its exact original source"), EditedSource != nullptr))
        return false;
    const FString CustodyBeforeEditResume = Meta->GetValue(EditedWorld, *Key);
    const auto EditResumed = P4ResumeOriginalLevelNative(EditedWorld, (*EditedSource)->Header, (*EditedSource)->Body);
    const auto ChangesAfterResume = P4AnalyzeOriginalLevelChanges(EditedWorld);
    if (!TestTrue(TEXT("Resume preserves creator edits and never adopts them into the original baseline"),
            EditResumed.IsValid() && EditResumed.Asset == EditedWorld && IsValid(Added) &&
                CustodyBeforeEditResume == Meta->GetValue(EditedWorld, *Key) && ChangesAfterResume.IsValid() &&
                ChangesAfterResume.Added.Num() == 1 && ChangesAfterResume.Added.Contains(Added->GetName()) &&
                ChangesAfterResume.Removed.Num() == 0))
    {
        AddError(EditResumed.ErrorDetail);
        AddError(ChangesAfterResume.ErrorDetail);
        return false;
    }
    FP4ExportModRequest Request;
    FP4ModNames Names;
    if (!P4ExportDriverTest::Request(
            *this, TEXT("native.gamebacked.test"), TEXT("Game-backed validation"), Request, Names))
        return false;
    Request.MapPackage = Root->Package;
    const FString SourceTail = Root->Package.Mid(6) + TEXT(".umap");
    FString NormalHeader = Header;
    FPaths::NormalizeFilename(NormalHeader);
    if (!TestTrue(TEXT("Actual source content root is derived from the canonical map identity"),
            NormalHeader.EndsWith(SourceTail)))
        return false;
    Request.ExtractedGameDirectory = NormalHeader.LeftChop(SourceTail.Len());
    for (const auto& Layer : Layers)
        Request.Roots.Add({EP4ExportRootKind::Package, Layer->Package});
    FP4ExportModResult Exported;
    if (!TestTrue(TEXT("Normal production exporter handles a game-backed map with unsupported originals"),
            P4ExportMod(Request, Exported, Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(TEXT("Export publishes a mod-owned addition level"),
            Exported.Published && Exported.Levels.Num() > 0 && Exported.Packages.Num() > 0))
        return false;
    for (const FString& Shipped : Exported.Packages)
        TestTrue(TEXT("Every shipped package is creator-owned; no original map replacement is packed"),
            Shipped.StartsWith(Names.PackageRoot + TEXT("/")));
    FP4ExportGameInventory Inventory;
    if (!TestTrue(TEXT("Independent verification reads the actual original game inventory"),
            P4ReadExportGameInventory(Request.Tools, Request.GamePaksDirectory,
                Request.WorkDirectory / TEXT("independent-census"), Inventory, Error)))
    {
        AddError(Error);
        return false;
    }
    FString Utoc;
    for (const FString& File : Exported.Files)
        if (File.EndsWith(TEXT(".utoc")))
            Utoc = FPaths::IsRelative(File) ? Exported.Directory / File : File;
    if (!TestFalse(TEXT("Published container has an IoStore table"), Utoc.IsEmpty()))
        return false;
    FP4ExportContainerCensus Census;
    if (!TestTrue(TEXT("Independent packed-container census reads exact published bytes"),
            P4InspectExportContainer(
                Request.Tools, Utoc, Request.WorkDirectory / TEXT("independent-census"), Inventory, Census, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("The packed container claims no original game packages"), Census.GameClaims.Num(), 0);
    for (const auto& Layer : Layers)
        TestFalse(TEXT("No original layer is present as an empty replacement"),
            Census.PackageIds.Contains(P4ExportPackageId(Layer->Package)));
    return !HasAnyErrors();
}
#endif

#include "P4KadanCreatorFlowStageList.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// CreatorReport: the creator platform's collision, material advice on owned meshes and an original collider.
class FCreatorReportStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

private:
    void AddCollisionSteps();

    void AddMaterialAdviceSteps();

    void AddOriginalColliderSteps();
};

// ExportBusy: deferred review and export clicks on a borrowed, hash-pinned creator texture and mod.
class FExportBusyStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

    void Finish() override;

    void Stop() override;

    FString QualificationScope() const override;

    void DescribeReceipt(FJsonObject& Receipt) const override;

private:
    bool RestoreBusyDefinition();

    bool BorrowedBusyFixture();

    FString BusyDefinitionFile, BusyAssetFile, BusyAssetSha;
    TArray<uint8> BusyDefinitionBytes;
};
} // namespace

void FCreatorReportStage::AddSteps()
{
    AddCollisionSteps();
    AddMaterialAdviceSteps();
    AddOriginalColliderSteps();
}

void FCreatorReportStage::AddCollisionSteps()
{
    Flow.Add(TEXT("Refresh the legacy room dependency through normal reimport"),
        [this]
        {
            FP4KadanSourceCatalog Catalog;
            FString Error;
            if (!P4KadanReadSourceCatalog(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")),
                    FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC")), Catalog, Error))
                return Flow.Expect(false, Error);
            auto Settings = Catalog.ImportSettings;
            Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
            Settings.AllowGameBackedLevels = true;
            const auto Plan = P4KadanPlanImport(
                Settings, {TEXT("/Game/Carrot4/Terrain/Cave/Type/Soil/BlueprintActor/CMA_T02_RM_5x4_002")}, false);
            const auto Result = P4KadanRunImport(Settings, Plan, false);
            FFileHelper::SaveStringToFile(Result.Report, *(Flow.Out / TEXT("ground-refresh.json")),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            return Flow.Expect(Result.Ok, TEXT("The legacy room dependency could not be refreshed"));
        });
    Flow.Add(TEXT("Load the retained Cave033 import"),
        [this]
        {
            return Flow.Expect(UEditorLoadingAndSavingUtils::LoadMap(
                                   FPlatformMisc::GetEnvironmentVariable(TEXT("P4_CREATOR_REPORT_MAP"))) != nullptr,
                TEXT("Cave033 did not open"));
        });
    Flow.Add(TEXT("Select the creator platform without collision"),
        [this]
        {
            UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Mods/CreatorReportVerify/OwnGround.OwnGround"));
            if (!Mesh || !P4HasCreatorSource(Mesh))
                return Flow.Expect(false, TEXT("An imported creator platform is required"));
            Mesh->CreateBodySetup();
            Mesh->BodySetup->AggGeom.EmptyElements();
            Mesh->BodySetup->CollisionTraceFlag = CTF_UseDefault;
            Mesh->BodySetup->InvalidatePhysicsData();
            Mesh->BodySetup->CreatePhysicsMeshes();
            auto* Actor = GEditor->GetEditorWorldContext().World()->SpawnActor<AStaticMeshActor>(
                FVector(200, 0, 50), FRotator::ZeroRotator);
            Actor->SetActorLabel(TEXT("CreatorReportVerifyGround"));
            Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
            GEditor->SelectNone(false, true);
            GEditor->SelectActor(Actor, true, true);
            Flow.Browser().SyncBrowserToAssets(TArray<UObject*>{Mesh});
            return true;
        });
    Flow.Add(TEXT("Open the actual selected asset panel"),
        [this] { return Flow.Menu(TEXT("Selected asset"), TEXT("What you can change")); });
    Flow.Add(TEXT("Inspect the creator platform"),
        [this] { return Flow.Click(TEXT("Inspect assets selected in Content Browser")); });
    Flow.Add(
        TEXT("Read imported ground choices"), [this] { return Flow.Click(TEXT("Refresh imported ground types")); });
    Flow.Add(TEXT("The actual imported cave supplies ground choices"),
        [this]
        {
            return Flow.Expect(Flow.ButtonEnabled(TEXT("Add terrain collision and use this ground type")),
                TEXT("The retained cave has no imported ground types"));
        });
    Flow.Add(TEXT("Capture the panel before applying collision"),
        [this] { return Flow.Screenshot(TEXT("collision-before")); });
    Flow.Add(TEXT("Apply collision through the actual panel button"),
        [this] { return Flow.Click(TEXT("Add terrain collision and use this ground type")); });
    Flow.Add(TEXT("Read back real collision and save the platform"),
        [this]
        {
            auto* Actor = Cast<AStaticMeshActor>(GEditor->GetSelectedActors()->GetTop(AStaticMeshActor::StaticClass()));
            if (!Actor)
                return Flow.Expect(false, TEXT("The selected platform is missing"));
            UStaticMeshComponent* Ground = nullptr;
            for (auto* Part : TInlineComponentArray<UStaticMeshComponent*>(Actor))
            {
                TArray<uint32> Tags;
                if (P4ReadGroundTags(Part, Tags))
                    Ground = Part;
            }
            if (!Flow.Expect(Ground && Ground->GetStaticMesh()->BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple,
                    TEXT("Panel did not add own triangle collision")))
                return true;
            FHitResult Hit;
            FCollisionQueryParams Query;
            if (!Flow.Expect(Ground->LineTraceComponent(Hit, FVector(400, 200, 400), FVector(400, 200, -100), Query),
                    TEXT("Simple gameplay trace missed the platform")))
                return true;
            return Flow.Expect(UEditorLoadingAndSavingUtils::SavePackages(
                                   {Ground->GetStaticMesh()->GetOutermost(), Actor->GetWorld()->GetOutermost()}, true),
                TEXT("Platform save failed"));
        });
    Flow.Add(
        TEXT("Capture the completed collision action"), [this] { return Flow.Screenshot(TEXT("collision-after")); });
}

void FCreatorReportStage::AddMaterialAdviceSteps()
{
    for (const FString Name :
        {FString(TEXT("BlackColors")), FString(TEXT("NoColors")), FString(TEXT("Good")), FString(TEXT("Stretched")),
            FString(TEXT("BadNormals")), FString(TEXT("StretchedUV")), FString(TEXT("MissingUV"))})
    {
        Flow.Add(TEXT("Select material control ") + Name,
            [this, Name]
            {
                UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *(TEXT("/Game/Mods/CreatorReportVerify/OwnWall") + Name));
                if (!Mesh)
                    return Flow.Expect(false, TEXT("Material control missing"));
                Flow.Browser().SyncBrowserToAssets(TArray<UObject*>{Mesh});
                return true;
            });
        Flow.Add(TEXT("Inspect material control ") + Name,
            [this] { return Flow.Click(TEXT("Inspect assets selected in Content Browser")); });
        Flow.Add(TEXT("Check the displayed material advice ") + Name,
            [this, Name]
            {
                if (Name == TEXT("BlackColors"))
                    return Flow.Expect(Flow.VisibleText(TEXT("Vertex alpha is below white"), true) &&
                            Flow.VisibleText(TEXT("Vertex red is below white"), true),
                        TEXT("Mask diagnostics were not displayed"));
                if (Name == TEXT("BadNormals"))
                    return Flow.Expect(Flow.VisibleText(TEXT("invalid tangent frames or opposing normals"), true),
                        TEXT("Normal diagnostics were not displayed"));
                if (Name == TEXT("StretchedUV"))
                    return Flow.Expect(Flow.VisibleText(TEXT("UV channel 0 is collapsed or severely stretched"), true),
                        TEXT("Active UV distortion was not displayed"));
                if (Name == TEXT("MissingUV"))
                    return Flow.Expect(Flow.VisibleText(TEXT("UV channel 3, which the mesh lacks"), true),
                        TEXT("Missing active UV channel was not displayed"));
                return Flow.Expect(Flow.VisibleText(TEXT("This cave wall material uses world position"), true) &&
                        !Flow.VisibleText(TEXT("Vertex alpha is below white"), true),
                    TEXT("White-mask control received incorrect advice"));
            });
        Flow.Add(TEXT("Capture material diagnostics ") + Name,
            [this, Name] { return Flow.Screenshot(TEXT("diagnostics-") + Name); });
    }
}

void FCreatorReportStage::AddOriginalColliderSteps()
{
    Flow.Add(TEXT("Select the original collider"),
        [this]
        {
            auto* Mesh = LoadObject<UStaticMesh>(nullptr,
                TEXT("/Game/Carrot4/Terrain/Cave/Type/Soil/Meshes/CMA_T02_RM_5x4_002/COL_CMA_T02_RM_5x4_002_W"));
            if (!Mesh)
                return Flow.Expect(false, TEXT("Original collider path missing"));
            Flow.Browser().SyncBrowserToAssets(TArray<UObject*>{Mesh});
            return true;
        });
    Flow.Add(TEXT("Inspect original own triangle collision"),
        [this] { return Flow.Click(TEXT("Inspect assets selected in Content Browser")); });
    Flow.Add(
        TEXT("Capture original collision explanation"), [this] { return Flow.Screenshot(TEXT("original-collider")); });
}

void FExportBusyStage::AddSteps()
{
    Flow.Add(TEXT("Verify borrowed existing texture and definitions without constructing or saving assets"),
        [this] { return BorrowedBusyFixture(); });
    Flow.Add(TEXT("Close a restored Kadan tab before the bounded UI gate"),
        [this]
        {
            auto Panel = Flow.Tab();
            return !Panel || Panel->RequestCloseTab();
        });
    Flow.Add(TEXT("Observe restored tab closed"), [this] { return !Flow.Tab(); });
    Flow.Add(
        TEXT("Open actual Mods and export panel"), [this] { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Choose the existing borrowed mod through its picker"), [this] { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Borrowed roots appear unchanged"), [this] { return Flow.CheckRoots(Flow.B, {Flow.BFolder}); });
    Flow.Add(TEXT("Choose an owned unused export destination"),
        [this] { return Flow.SetField(TEXT("Output folder"), Flow.Out / TEXT("must-not-publish")); });
    Flow.Add(
        TEXT("Review actual existing creator selection"),
        [this] { return Flow.ClickQueuedExport(TEXT("Review export")); }, 180);
    Flow.Add(TEXT("Ordinary validation genuinely enables Export"),
        [this]
        {
            return Flow.Expect(Flow.ButtonEnabled(TEXT("Export mod")),
                TEXT("Borrowed existing creator asset did not pass ordinary export review"));
        });
    Flow.Add(TEXT("Change only the unsaved version through real input"),
        [this] { return Flow.SetField(TEXT("Version"), TEXT("2.0.0")); });
    Flow.Add(TEXT("Unsaved identity correctly invalidates export readiness"), [this]
        { return Flow.Expect(!Flow.ButtonEnabled(TEXT("Export mod")), TEXT("Unsaved identity left Export enabled")); });
    Flow.Add(TEXT("Review unsaved identity through its actual queued button"),
        [this] { return Flow.ClickQueuedExport(TEXT("Review export")); });
    Flow.Add(TEXT("Unsaved identity returns its normal review error"),
        [this]
        {
            return Flow.Expect(
                Flow.TaggedText(TEXT("KadanExportReview")).Contains(TEXT("Save the mod before reviewing its export")) &&
                    !Flow.OperationBusy(),
                TEXT("Queued review did not finish with its real identity refusal"));
        });
    Flow.Add(TEXT("Restore original version through real input"),
        [this] { return Flow.SetField(TEXT("Version"), Flow.B.Version); });
    Flow.Add(
        TEXT("Re-review the original valid selection"),
        [this] { return Flow.ClickQueuedExport(TEXT("Review export")); }, 180);
    Flow.Add(TEXT("Real re-review enables Export before stale-definition control"),
        [this]
        {
            return Flow.Expect(Flow.ButtonEnabled(TEXT("Export mod")),
                TEXT("Restored identity did not regain ordinary reviewed readiness"));
        });
    Flow.Add(TEXT("Change only the owned copied persisted definition after review"),
        [this]
        {
            FP4ModDescriptor Changed = Flow.B;
            Changed.Version = TEXT("2.0.0");
            FString Error;
            return Flow.Expect(P4WriteModDescriptor(FPaths::ProjectDir(), Changed, &Flow.B, Error),
                TEXT("Cannot stage owned persisted-identity mismatch: ") + Error);
        });
    Flow.Add(
        TEXT("Click genuine enabled Export before mandatory revalidation refuses"),
        [this] { return Flow.ClickQueuedExport(TEXT("Export mod")); }, 180);
    Flow.Add(TEXT("Stale definition refuses before cooking or publication"),
        [this]
        {
            return Flow.Expect(!Flow.OperationBusy() &&
                    Flow.TaggedText(TEXT("KadanStatus"))
                        .Contains(TEXT("Export needs the current persisted project mod descriptor")) &&
                    !IFileManager::Get().DirectoryExists(*(Flow.Out / TEXT("must-not-publish"))),
                TEXT("Queued export did not report its real persisted-identity refusal before publication"));
        });
    Flow.Add(TEXT("Restore exact borrowed descriptor bytes"),
        [this] { return Flow.Expect(RestoreBusyDefinition(), TEXT("Borrowed descriptor restoration failed")); });
    Flow.Add(TEXT("Retain bounded final refusal screenshot"),
        [this] { return Flow.Screenshot(TEXT("export-busy-refusal")); });
    Flow.Add(TEXT("Close only the test's Kadan panel"),
        [this]
        {
            auto Panel = Flow.Tab();
            return !Panel || Panel->RequestCloseTab();
        });
}

void FExportBusyStage::Finish()
{
    if (!RestoreBusyDefinition())
        Flow.Fail(TEXT("Owned copied descriptor was not restored"));
    if (!BusyAssetFile.IsEmpty())
    {
        FString Actual;
        int64 Bytes = 0;
        if (!P4HashFile(BusyAssetFile, 1024 * 1024, Bytes, Actual) || !P4ExactNativeString(Actual, BusyAssetSha))
            Flow.Fail(TEXT("Borrowed creator texture bytes changed during the busy-click gate"));
    }
}

void FExportBusyStage::Stop()
{
    RestoreBusyDefinition();
}

FString FExportBusyStage::QualificationScope() const
{
    return TEXT("Actual deferred review/export clicks, ordinary readiness and intentional persisted-identity "
                "refusal; reused existing creator bytes, no import, asset save, cook, publication or game claim");
}

void FExportBusyStage::DescribeReceipt(FJsonObject& Receipt) const
{
    Receipt.SetStringField(TEXT("borrowed_creator_sha256"), BusyAssetSha);
}

bool FExportBusyStage::RestoreBusyDefinition()
{
    if (BusyDefinitionBytes.Num() == 0)
        return true;
    TArray<uint8> Current;
    if (FFileHelper::LoadFileToArray(Current, *BusyDefinitionFile) && Current == BusyDefinitionBytes)
        return true;
    const bool Restored = FFileHelper::SaveArrayToFile(BusyDefinitionBytes, *BusyDefinitionFile);
    if (!Restored)
        Flow.Test.AddError(TEXT("Cannot restore the owned borrowed mod definition"));
    return Restored;
}

bool FExportBusyStage::BorrowedBusyFixture()
{
    FString Error;
    FP4ModDescriptor Mod;
    TArray<FP4ExportRoot> Roots;
    BusyAssetFile = FPackageName::LongPackageNameToFilename(Flow.BPackage, FPackageName::GetAssetPackageExtension());
    BusyAssetSha = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_EXPORT_BUSY_ASSET_SHA256"));
    FString Actual;
    int64 Bytes = 0;
    BusyDefinitionFile = FPaths::ProjectDir() / TEXT("Config/KadanMods") / (Flow.B.Id + TEXT(".json"));
    if (!P4ReadModDescriptor(FPaths::ProjectDir(), Flow.B.Id, Mod, Error) || Mod.Name != Flow.B.Name ||
        Mod.Version != Flow.B.Version || !P4ReadExportRoots(FPaths::ProjectDir(), Mod, Roots, Error) ||
        Roots.Num() != 1 || Roots[0].Kind != EP4ExportRootKind::Folder || Roots[0].Path != Flow.BFolder ||
        BusyAssetSha.Len() != 64 || !P4HashFile(BusyAssetFile, 1024 * 1024, Bytes, Actual) ||
        !P4ExactNativeString(Actual, BusyAssetSha) ||
        !FFileHelper::LoadFileToArray(BusyDefinitionBytes, *BusyDefinitionFile))
    {
        Flow.Fail(TEXT("ExportBusy requires the hash-pinned existing creator texture and exact borrowed second-mod "
                       "definition/roots: ") +
            Error);
        return true;
    }
    return true;
}

TUniquePtr<FFlowStage> MakeCreatorReportStage(FFlow& Flow)
{
    return MakeUnique<FCreatorReportStage>(Flow);
}

TUniquePtr<FFlowStage> MakeExportBusyStage(FFlow& Flow)
{
    return MakeUnique<FExportBusyStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

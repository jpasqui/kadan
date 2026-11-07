#pragma once
#include "P4KadanCreatorFlow.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{
// Each stage file makes its own stage. The comments name the canonical stages each one serves.

// Setup
TUniquePtr<FFlowStage> MakeSetupStage(FFlow &Flow);
// SetupReady
TUniquePtr<FFlowStage> MakeSetupReadyStage(FFlow &Flow);
// Browser
TUniquePtr<FFlowStage> MakeBrowserStage(FFlow &Flow);
// Reimport
TUniquePtr<FFlowStage> MakeReimportStage(FFlow &Flow);
// Create
TUniquePtr<FFlowStage> MakeCreateStage(FFlow &Flow);
// Reopen, and any stage no other maker serves
TUniquePtr<FFlowStage> MakeReopenStage(FFlow &Flow);
// CreatorReport
TUniquePtr<FFlowStage> MakeCreatorReportStage(FFlow &Flow);
// ExportBusy
TUniquePtr<FFlowStage> MakeExportBusyStage(FFlow &Flow);
// IssuesBefore, Issues and Progress
TUniquePtr<FFlowStage> MakeIssuesStage(FFlow &Flow);
// ShippingBefore and Shipping
TUniquePtr<FFlowStage> MakeShippingStage(FFlow &Flow);
// MapProgress
TUniquePtr<FFlowStage> MakeMapProgressStage(FFlow &Flow);
// IdentityBefore and Identity
TUniquePtr<FFlowStage> MakeIdentityStage(FFlow &Flow);
// Pages, MapLayersBefore, MapLayers and MapLayersDistinct
TUniquePtr<FFlowStage> MakeLayersStage(FFlow &Flow);
// EdenBefore and Eden
TUniquePtr<FFlowStage> MakeEdenStage(FFlow &Flow);
// GroundBefore and Ground
TUniquePtr<FFlowStage> MakeGroundStage(FFlow &Flow);
// MaterialBefore, Material and MaterialReopen
TUniquePtr<FFlowStage> MakeMaterialStage(FFlow &Flow);
// Receipt and ReceiptReopen
TUniquePtr<FFlowStage> MakeReceiptStage(FFlow &Flow);
// Draft
TUniquePtr<FFlowStage> MakeDraftStage(FFlow &Flow);
// LastMod and LastModReopen
TUniquePtr<FFlowStage> MakeLastModStage(FFlow &Flow);
// HistoryExport
TUniquePtr<FFlowStage> MakeHistoryStage(FFlow &Flow);
// Upgrade and UpgradeReopen
TUniquePtr<FFlowStage> MakeUpgradeStage(FFlow &Flow);
// Copy and CopyRead
TUniquePtr<FFlowStage> MakeCopyStage(FFlow &Flow);
// CopyExtras
TUniquePtr<FFlowStage> MakeCopyExtrasStage(FFlow &Flow);
} // namespace P4KadanCreatorFlow
#endif

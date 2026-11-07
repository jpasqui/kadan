#pragma once

#include "CoreMinimal.h"

inline bool P4KadanCanonicalCreatorFlowStage(FString& Stage)
{
    for (const TCHAR* Candidate : {
        TEXT("Browser"),
        TEXT("Setup"),
        TEXT("SetupReady"),
        TEXT("Create"),
        TEXT("Reopen"),
        TEXT("Reimport"),
        TEXT("CreatorReport"),
        TEXT("ExportBusy"),
        TEXT("ShippingBefore"),
        TEXT("Shipping"),
        TEXT("IssuesBefore"),
        TEXT("Issues"),
        TEXT("Progress"),
        TEXT("MapProgress"),
        TEXT("IdentityBefore"),
        TEXT("Identity"),
        TEXT("MapLayersBefore"),
        TEXT("MapLayers"),
        TEXT("MapLayersDistinct"),
        TEXT("Pages"),
        TEXT("EdenBefore"),
        TEXT("Eden"),
        TEXT("GroundBefore"),
        TEXT("Ground"),
        TEXT("MaterialBefore"),
        TEXT("Material"),
        TEXT("MaterialReopen"),
        TEXT("Receipt"),
        TEXT("ReceiptReopen"),
        TEXT("Draft"),
        TEXT("LastMod"),
        TEXT("LastModReopen"),
        TEXT("HistoryExport"),
        TEXT("Upgrade"),
        TEXT("UpgradeReopen"),
        TEXT("Copy"),
        TEXT("CopyExtras"),
        TEXT("CopyRead")})
    {
        if (Stage.Equals(Candidate, ESearchCase::IgnoreCase))
        {
            Stage = Candidate;
            return true;
        }
    }
    return false;
}

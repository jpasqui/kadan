#pragma once
#include "P4MaterialUsedResources.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/material_used_resources.h"
THIRD_PARTY_INCLUDES_END
// Same module only. The FP4MaterialRecovery method supplies its existing held
// archive and raw resolver; no second archive parse/read or UObject admission.
FP4MaterialUsedResources P4DiscoverMaterialUsedResourcesNative(const FP4MaterialFamilySources&,
    const P4MapKit::Material::MaterialOriginalImportRawResolver&,
    const P4MapKit::Material::MaterialOriginalArchiveAccess&,
    const FString& HeldArchiveSha256,const TFunction<bool(FString&)>& SourceCurrent);
// The owner graph a complete used-resource trace already recovered from the same reads, archive and
// parameter collections. A master family's recovery takes it instead of recovering the owner again.
bool P4FindTracedMaterialOwnerGraph(const std::string& Owner,const P4MapKit::Material::MaterialOriginalImportRawResolver& Read,
    const FString& HeldArchiveSha256,const std::vector<std::string>& Collections,P4MapKit::Material::Json& Graph);
int32 P4TracedMaterialOwnerGraphHits();

#pragma once
#include "Modules/ModuleInterface.h"
#include "IPlacementModeModule.h"

class FKadanModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    // Registered shader layouts live until process exit; close the editor before replacing this module's DLL.
    virtual bool SupportsDynamicReloading() override { return false; }
    int32 Refresh(bool bLoadAssets = false);
private:
    void AfterEngineInit();
    TArray<FPlacementModeID> Items;
    FDelegateHandle EngineInitHandle;
    FDelegateHandle BlueprintHandle;
    FDelegateHandle MapHandle;
    FDelegateHandle LayersHandle;
    FDelegateHandle LevelAddedHandle;
    FDelegateHandle PreSaveHandle;
    FDelegateHandle PostSaveHandle;
    FDelegateHandle ActorAddedHandle;
    FDelegateHandle OriginalAssetLoadedHandle;
    bool bRefreshing = false;
};

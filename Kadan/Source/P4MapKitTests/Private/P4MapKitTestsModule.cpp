#include "Modules/ModuleManager.h"

void P4StartPrivateNativeAutomationPacing();
void P4StopPrivateNativeAutomationPacing();
void P4StartMaterialParentProbe();

class FP4MapKitTestsModule final : public IModuleInterface
{
public:
    void StartupModule() override
    {
        P4StartPrivateNativeAutomationPacing();
        P4StartMaterialParentProbe();
    }
    void ShutdownModule() override { P4StopPrivateNativeAutomationPacing(); }
};
IMPLEMENT_MODULE(FP4MapKitTestsModule, P4MapKitTests);

#include "P4KadanLastMod.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
const TCHAR* Section = TEXT("Kadan");
const TCHAR* Key = TEXT("LastOpenedModId");
}

FString P4KadanLastModId(const FString& SettingsFile)
{
    FString Id;
    if (GConfig)
    {
        GConfig->GetString(Section, Key, Id, SettingsFile);
    }
    return Id;
}

void P4KadanRememberModId(const FString& SettingsFile, const FString& Id)
{
    // A new unsaved draft keeps the last saved identity as the reopen target.
    if (!GConfig || Id.IsEmpty() || P4KadanLastModId(SettingsFile) == Id)
    {
        return;
    }
    GConfig->SetString(Section, Key, *Id, SettingsFile);
    GConfig->Flush(false, SettingsFile);
}

TSharedPtr<FP4ModDescriptor> P4KadanFindRememberedMod(
    const FString& Id, const TArray<TSharedPtr<FP4ModDescriptor>>& Mods)
{
    for (const TSharedPtr<FP4ModDescriptor>& Mod : Mods)
    {
        if (Mod.IsValid() && Mod->Id == Id)
        {
            return Mod;
        }
    }
    return nullptr;
}

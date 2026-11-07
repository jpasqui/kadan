#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class UWorld;

// A normal native importer may construct at a saved path only while this scope owns the
// previously imported package. Creator content is preserved, and failed imports restore identity.
bool P4KadanOwnsReimportSlot(const FString& Package);
class FP4KadanReimportAsset
{
public:
    explicit FP4KadanReimportAsset(const FString& Package);
    ~FP4KadanReimportAsset();
    bool IsReady() const { return Error.IsEmpty(); }
    bool Preserved() const { return KeepExisting; }
    const FString& GetError() const { return Error; }
    bool Finish(FString& Why);
private:
    bool Restore();
    FString Name, Path, Retired, Stage, Error;
    TStrongObjectPtr<UObject> Original;
    TStrongObjectPtr<UPackage> PreviousPackage;
    uint32 OriginalFlags = 0;
    bool Active = false, KeepExisting = false;
};

// Check pending level work before any import can update references/reinstance actors. Dirtiness
// created by those native operations is then safe to replay against the captured creator content.
class FP4KadanReimportScope
{
public:
    explicit FP4KadanReimportScope(bool Enabled, const TSet<FString>& Worlds = TSet<FString>());
    ~FP4KadanReimportScope();
    FString Error;
private:
    bool Active = false;
};
bool P4KadanHasSavedLevelReimportScope();
// Capture selected saved levels before a dependency can reinstance their actors. A nested import
// cannot begin a new capture after the outer import has already changed dependencies.
bool P4CaptureSavedLevelReimport(UWorld* World, FString& Error);
void P4ClearSavedLevelReimports();

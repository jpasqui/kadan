#pragma once
#include "CoreMinimal.h"
class UPackage;
// C++ native import publication. Not a Blueprint/Python editor permission toggle.
namespace P4ImportPublication
{
class P4BLUEPRINTIMPORT_API FSaveScope
{
public:
    explicit FSaveScope(UPackage* Package);
    ~FSaveScope();
    FSaveScope(const FSaveScope&) = delete;
    FSaveScope& operator=(const FSaveScope&) = delete;
private:
    UPackage* Package;
};
P4BLUEPRINTIMPORT_API bool IsSaving(UPackage* Package);

// Held while Kadan's importer makes or fills game assets. The read-only guards leave what the importer
// writes alone while one is held.
class P4BLUEPRINTIMPORT_API FImportScope
{
public:
    FImportScope();
    ~FImportScope();
    FImportScope(const FImportScope&) = delete;
    FImportScope& operator=(const FImportScope&) = delete;
};
P4BLUEPRINTIMPORT_API bool IsImporting();
}

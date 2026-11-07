#include "P4OriginalImportSave.h"
namespace P4ImportPublication
{
namespace { TMap<UPackage*, int32> Saves; int32 Imports = 0; }
FSaveScope::FSaveScope(UPackage* InPackage) : Package(InPackage)
{ check(IsInGameThread() && Package); ++Saves.FindOrAdd(Package); }
FSaveScope::~FSaveScope()
{ if (--Saves.FindChecked(Package) == 0) Saves.Remove(Package); }
bool IsSaving(UPackage* Package) { return IsInGameThread() && Saves.Contains(Package); }
FImportScope::FImportScope() { check(IsInGameThread()); ++Imports; }
FImportScope::~FImportScope() { --Imports; }
bool IsImporting() { return IsInGameThread() && Imports > 0; }
}

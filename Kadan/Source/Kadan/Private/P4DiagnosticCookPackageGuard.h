#pragma once
#include "CoreMinimal.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

enum class EP4DiagnosticCookPackageError : uint8
{ None, EditorThreadRequired, PackageRequired, MetadataNotRestored, MaterialStateSchemaUnsupported, MaterialStateNotRestored };

// One diagnostic SavePackage scope. The caller must finish asynchronous writes
// and explicitly check restoration before admitting any cooked output.
// Retains package metadata and the known MaterialInterface texture-streaming
// state that its cooked PreSave sorts in place. All other native asset data
// remains subject to the caller's independent source/preservation comparison.
class KADAN_API FP4DiagnosticCookPackageGuard
{
public:
    explicit FP4DiagnosticCookPackageGuard(UPackage* InPackage);
    ~FP4DiagnosticCookPackageGuard();
    FP4DiagnosticCookPackageGuard(const FP4DiagnosticCookPackageGuard&) = delete;
    FP4DiagnosticCookPackageGuard& operator=(const FP4DiagnosticCookPackageGuard&) = delete;
    bool IsReady() const { return Ready; }
    bool AwaitAndRestore(FString& Why);
    EP4DiagnosticCookPackageError GetError() const { return Error; }
    const FString& GetErrorDetail() const { return ErrorDetail; }
private:
    struct FMaterialState;
    TUniquePtr<FMaterialState> Materials;
    TStrongObjectPtr<UPackage> Package;
    uint32 Flags = 0;
    uint64 FileSize = 0;
    FName FileName;
    FGuid Guid;
#if WITH_EDITORONLY_DATA
    FGuid PersistentGuid;
#endif
    bool Dirty = false, Ready = false, Completed = false;
    EP4DiagnosticCookPackageError Error = EP4DiagnosticCookPackageError::None;
    FString ErrorDetail;
};

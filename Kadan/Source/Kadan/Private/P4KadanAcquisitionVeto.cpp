#include "P4KadanAcquisitionVeto.h"
#if P4_KADAN_TEST_SEAMS
#include "Misc/Paths.h"

namespace P4KadanAcquisitionVetoPrivate
{
FP4KadanScopedAcquisitionVeto* Current = nullptr;
FString Normalize(const FString& Path)
{
    FString Result = FPaths::ConvertRelativePathToFull(Path);
    FPaths::CollapseRelativeDirectories(Result);
    FPaths::NormalizeDirectoryName(Result);
    return Result;
}
} //namespace P4KadanAcquisitionVetoPrivate

FP4KadanScopedAcquisitionVeto::FP4KadanScopedAcquisitionVeto(const FString& OwnerProject)
{
    if (!IsInGameThread() || OwnerProject.IsEmpty())
        return;
    Project = P4KadanAcquisitionVetoPrivate::Normalize(OwnerProject);
    if (Project != P4KadanAcquisitionVetoPrivate::Normalize(FPaths::ProjectDir()))
        return;
    Previous = P4KadanAcquisitionVetoPrivate::Current;
    P4KadanAcquisitionVetoPrivate::Current = this;
    Active = true;
}
FP4KadanScopedAcquisitionVeto::~FP4KadanScopedAcquisitionVeto()
{
    if (!Active)
        return;
    check(IsInGameThread() && P4KadanAcquisitionVetoPrivate::Current == this);
    P4KadanAcquisitionVetoPrivate::Current = Previous;
}
bool P4KadanSourceAcquisitionAllowed(const TArray<FString>& Packages, FString& Error)
{
    if (!IsInGameThread() || !Packages.Num())
        return true;
    auto* Scope = P4KadanAcquisitionVetoPrivate::Current;
    if (!Scope || Scope->Project != P4KadanAcquisitionVetoPrivate::Normalize(FPaths::ProjectDir()))
        return true;
    ++Scope->Denied;
    for (const FString& Package : Packages)
        Scope->Packages.Add(Package);
    Error = TEXT("The current dev fixture forbids unexpected source acquisition before conversion: ") +
        FString::Join(Packages, TEXT(", ")).Left(2048);
    return false;
}
void* P4BeginKadanAcquisitionVeto(const FString& OwnerProject)
{
    return new FP4KadanScopedAcquisitionVeto(OwnerProject);
}
void P4EndKadanAcquisitionVeto(void* Scope)
{
    delete static_cast<FP4KadanScopedAcquisitionVeto*>(Scope);
}
bool P4KadanAcquisitionVetoActive(const void* Scope)
{
    return static_cast<const FP4KadanScopedAcquisitionVeto*>(Scope)->IsActive();
}
int32 P4KadanAcquisitionVetoDenied(const void* Scope)
{
    return static_cast<const FP4KadanScopedAcquisitionVeto*>(Scope)->DeniedBatches();
}
const TSet<FString>* P4KadanAcquisitionVetoPackages(const void* Scope)
{
    return &static_cast<const FP4KadanScopedAcquisitionVeto*>(Scope)->DeniedPackages();
}
#endif

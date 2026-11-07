#include "P4ImportReferences.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"

namespace
{
const FP4ImportReferenceScope* P4ActiveReferenceScope = nullptr;
}
FP4ImportReferenceScope::FP4ImportReferenceScope(const TMap<FString, UObject*>& References)
    : Previous(P4ActiveReferenceScope)
{
    check(IsInGameThread());
    for (const auto& Entry : References)
    {
        Forward.Add(Entry.Key.ToLower(), Entry.Value);
        Reverse.Add(Entry.Value, Entry.Key);
    }
    P4ActiveReferenceScope = this;
}
FP4ImportReferenceScope::~FP4ImportReferenceScope()
{
    check(IsInGameThread());
    P4ActiveReferenceScope = Previous;
}
UObject* FP4ImportReferenceScope::Find(const FString& Path) const
{
    const auto* Found = Forward.Find(Path.ToLower());
    return Found ? *Found : nullptr;
}
FString FP4ImportReferenceScope::Path(UObject* Object) const
{
    const auto* Found = Reverse.Find(Object);
    return Found ? *Found : FString();
}
UObject* P4ImportFindObject(const FString& Path)
{
    if (IsInGameThread() && P4ActiveReferenceScope)
        if (UObject* Found = P4ActiveReferenceScope->Find(Path))
            return Found;
    return StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn);
}
FString P4ImportStagedPath(UObject* Object)
{
    return IsInGameThread() && P4ActiveReferenceScope ? P4ActiveReferenceScope->Path(Object) : FString();
}
FString P4ImportObjectPath(UObject* Object)
{
    const FString Logical = P4ImportStagedPath(Object);
    return Logical.IsEmpty() ? (Object ? Object->GetPathName() : FString()) : Logical;
}
FString P4ImportTextPath(const FString& Logical)
{
    UObject* Found = IsInGameThread() && P4ActiveReferenceScope ? P4ActiveReferenceScope->Find(Logical) : nullptr;
    return Found ? Found->GetPathName() : Logical;
}

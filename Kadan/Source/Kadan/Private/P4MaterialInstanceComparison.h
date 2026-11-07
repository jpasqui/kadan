#pragma once
#include "CoreMinimal.h"
#include "P4KadanImportSources.h"
class UObject;
class UMaterialInstanceConstant;
class FP4MaterialRecovery;
struct FP4OriginalRecord;

// Optional source-bound object-lines projection, after the ordinary cook/Lines and
// before its unchanged Digest. Only original MIC parameter ExpressionGUID leaves
// can be projected from a proven current recovered-parent frame to original source.
// Never touches an object, renderer, baseline, or any other comparison field.
// False always preserves Lines. Ordinary missing scope/not-applicable is silent;
// malformed present custody or mismatched binding returns its reason in Error.
KADAN_API bool P4ProjectOriginalMaterialInstanceBindings(UObject* Asset,TArray<FString>& Lines,FString& Error);

// Fresh recovered instances have a distinct contract. The complete ordinary
// cooked object-lines digest and exact metadata presence/text are hashed; no
// source GUID projection or historic record recapture is performed here.
struct FP4MaterialInstanceNativeEvidence
{
    FString NativeDigest,TransportDigest,CustodyDigest,SourceSha256,RecipeMd5;
    bool HasTransport=false,HasCustody=false;
    int32 TransportLength=0,CustodyLength=0;
};
KADAN_API FString P4MaterialInstanceComparisonState(UMaterialInstanceConstant* Instance,FString& Error,
    FP4MaterialInstanceNativeEvidence* Evidence=nullptr,TArray<FString>* NativeLines=nullptr);

// Only actual Recovery::Import can construct this authority, after its Recover
// and current raw-read witness. Ordinary public JSON builders cannot mint it.
class FP4RecoveredMaterialInstanceProof
{
    friend class FP4MaterialRecovery;
    FP4RecoveredMaterialInstanceProof(const FString& Package,const FString& Recipe,
        FP4KadanSourceBinding&& Source,const TFunction<bool(FString&)>& CurrentReads);
    FString Package,Recipe;
    FP4KadanSourceBinding Source;
    TFunction<bool(FString&)> CurrentReads;
public:
    FP4RecoveredMaterialInstanceProof(const FP4RecoveredMaterialInstanceProof&)=delete;
    FP4RecoveredMaterialInstanceProof& operator=(const FP4RecoveredMaterialInstanceProof&)=delete;
    bool Current(const FString& InstanceJson,const FString& Destination,FString& Error)const;
    // The original export's spelling, read only from this private raw-source proof.
    FString SourceObjectName()const;
    bool CaptureFresh(UMaterialInstanceConstant* Instance,const FString& InstanceJson,FP4OriginalRecord& Record,FString& Error)const;
};

FString P4BuildRecoveredMaterialInstanceReport(const FString& InstanceJson,const FString& Package,bool SaveToDisk,
    const FP4RecoveredMaterialInstanceProof& Proof,const TFunction<bool(FString&)>& SourceCurrent);

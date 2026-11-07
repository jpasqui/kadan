#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class UWidgetBlueprint;

namespace P4WidgetMembers
{
using FValidateSource=TFunction<bool(const FString&,const TSharedPtr<FJsonObject>&,FString&)>;
// Caller owns authentic immutable package inputs; JSON cannot install this scope.
class P4BLUEPRINTIMPORT_API FSourceScope
{
public:
    explicit FSourceScope(const FValidateSource& In);
    ~FSourceScope();
    FSourceScope(const FSourceScope&)=delete;
    FSourceScope& operator=(const FSourceScope&)=delete;
private:
    const FValidateSource* Previous=nullptr;
};
P4BLUEPRINTIMPORT_API bool Preflight(const TSharedPtr<FJsonObject>& Plan,UObject* Existing,FString& Why);
P4BLUEPRINTIMPORT_API bool ApplyVariableIntent(UWidgetBlueprint* Blueprint,const TSharedPtr<FJsonObject>& Plan,FString& Why);
P4BLUEPRINTIMPORT_API bool SourceFieldFlagsMatch(uint64 Live,uint64 Source);
bool VerifyCompiled(UWidgetBlueprint* Blueprint,const TSharedPtr<FJsonObject>& Plan,bool CheckDefaults,FString& Why);
}

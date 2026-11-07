#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/property_decoder.h"

class FProperty;
class FObjectPropertyBase;

// Typed native value writer shared by original levels, assets and tables.
// The resolver supplies only already-created objects local to the source package.
class FP4DecodedPropertyWriter
{
public:
    FP4DecodedPropertyWriter(TFunction<UObject*(const FString&)> Resolver, bool IsFinal)
        : Final(IsFinal), ResolveOwn(MoveTemp(Resolver))
    {}
    bool Final = false, Later = false;
    FString Error;
    bool Lenient = false;
    // For an asset kept for reference only: a reference to an asset the project does not have is left
    // empty and told with the values not held, instead of refused.
    bool MissingAsNotHeld = false;
    TArray<FString> NotHeld, NotShipped;

    KADAN_API bool Value(const FProperty* Property, void* Data, const P4MapKit::FDecodedValue& Said,
        const FString& Where, int32 Depth = 0);

private:
    TFunction<UObject*(const FString&)> ResolveOwn;
    static FString Text(const std::string& In);
    UObject* Own(const FString& Path) const { return ResolveOwn ? ResolveOwn(Path) : nullptr; }
    bool Fail(const FString& Why, const FString& Where)
    {
        if (Error.IsEmpty())
            Error = Why + TEXT(": ") + Where;
        return false;
    }
    bool Object(
        const FObjectPropertyBase* Property, void* Data, const P4MapKit::FDecodedValue& Said, const FString& Where);
};

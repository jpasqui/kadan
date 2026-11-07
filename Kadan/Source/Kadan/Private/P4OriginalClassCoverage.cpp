#include "P4OriginalClassCoverage.h"

bool P4StrictOriginalClassDefaultsQualified(
    const FP4OriginalClassComparisonResult& Result, EP4OriginalClassDefaultObservation Observation, FString& Reason)
{
    const auto Refuse = [&](const TCHAR* Message)
    {
        Reason = Message;
        return false;
    };
    if (Observation != EP4OriginalClassDefaultObservation::NativeStoredDefaults)
        return Refuse(TEXT("class-default-proof-is-temporary-cook-adaptation"));
    if (!Result.IsValid() || !Result.DefaultsReadable)
        return Refuse(TEXT("class-default-proof-source-or-native-data-unreadable"));
    if (!Result.PackageKnown || !Result.ValuesKnown || !Result.TemplatesKnown || !Result.ComparedKnown ||
        Result.Compared <= 0)
        return Refuse(TEXT("class-default-proof-coverage-unknown"));
    if (Result.ValuesNotHeld != 0)
    {
        Reason = FString::Printf(TEXT("class-default-proof-has-values-not-held: %d"), Result.ValuesNotHeld);
        return false;
    }
    if (!Result.Same || Result.Differences != 0 || Result.OnlyGameSays.Num() || Result.OnlyProjectSays.Num())
        return Refuse(TEXT("class-default-proof-has-unclosed-differences"));
    const auto Digest = [](const FString& Value)
    {
        if (Value.Len() != 64)
            return false;
        for (const TCHAR C : Value)
            if (!FChar::IsHexDigit(C))
                return false;
        return true;
    };
    if (!Digest(Result.GameDefaultFactsDigest) || !Digest(Result.DefaultFactsDigest) ||
        !Result.GameDefaultFactsDigest.Equals(Result.DefaultFactsDigest, ESearchCase::CaseSensitive))
        return Refuse(TEXT("class-default-proof-native-facts-digest-unbound"));
    Reason.Reset();
    return true;
}

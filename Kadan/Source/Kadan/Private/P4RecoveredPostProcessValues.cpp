#include "P4RecoveredPostProcessValues.h"
#include "P4RecoveredPostProcessStorage.h"
#include "P4DecodedPropertyWriter.h"
#include "UObject/UnrealType.h"

bool P4RecoveredPostProcessOriginalMemberIndex(const FString& Original, FString& Name, int32& Place)
{
    Name = Original;
    Place = 0;
    int32 Bracket = INDEX_NONE;
    if (!Name.FindChar(TEXT('['), Bracket))
        return !Name.IsEmpty();
    if (!Name.EndsWith(TEXT("]")) || Bracket == 0)
        return false;
    const FString Number = Name.Mid(Bracket + 1, Name.Len() - Bracket - 2);
    if (Number.IsEmpty() || Number.Len() > 10)
        return false;
    for (const TCHAR Digit : Number)
        if (Digit < TEXT('0') || Digit > TEXT('9'))
            return false;
    const uint64 Index = FCString::Strtoui64(*Number, nullptr, 10);
    if (Index > uint64(MAX_int32))
        return false;
    Place = int32(Index);
    Name.LeftInline(Bracket);
    return true;
}

EP4RecoveredPostProcessWrite P4WriteRecoveredPostProcessSettings(FP4DecodedPropertyWriter& Writer, UObject* Target,
    const FProperty* Property, int32 Place, const P4MapKit::FDecodedValue& Value, const FString& Where)
{
    using V = P4MapKit::FDecodedValue;
    if (!Target || !Property ||
        (Property->GetFName() != TEXT("Settings") && Property->GetFName() != TEXT("ForkSettings")))
        return EP4RecoveredPostProcessWrite::NotApplicable;
    const auto Refuse = [&](const FString& Why)
    {
        if (Writer.Error.IsEmpty())
            Writer.Error = Why + TEXT(": ") + Where;
        return EP4RecoveredPostProcessWrite::Refused;
    };
    FP4RecoveredPostProcessStorage Storage;
    FString Error;
    if (!P4GetRecoveredPostProcessStorage(Target->GetClass(), Storage, Error))
        return Error.IsEmpty() ? EP4RecoveredPostProcessWrite::NotApplicable : Refuse(Error);
    // ForkSettings is host storage, never a property path supplied by the game.
    if (Property != Storage.StockSettings || Place != 0)
        return Refuse(TEXT("postprocess-original-settings-path-required"));
    if (Value.Kind != V::EKind::Object)
        return Refuse(TEXT("level-import-postprocess-value-is-not-a-struct"));
    TSet<FString> Seen;
    for (const auto& Child : Value.Fields)
    {
        const FString Original = UTF8_TO_TCHAR(Child.first.c_str());
        FString Name;
        int32 MemberPlace;
        if (!P4RecoveredPostProcessOriginalMemberIndex(Original, Name, MemberPlace))
            return Refuse(TEXT("postprocess-invalid-fixed-member: ") + Original);
        const FString Identity = Name.ToLower() + FString::Printf(TEXT("[%d]"), MemberPlace);
        if (Seen.Contains(Identity))
            return Refuse(TEXT("postprocess-duplicate-original-member: ") + Original);
        Seen.Add(Identity);
    }
    for (const auto& Child : Value.Fields)
    {
        const FString Original = UTF8_TO_TCHAR(Child.first.c_str());
        const FString ChildPath = Where + TEXT(".") + Original;
        FString Name;
        int32 MemberPlace;
        P4RecoveredPostProcessOriginalMemberIndex(
            Original, Name, MemberPlace); // preflight checked all syntax/duplicates
        FP4RecoveredPostProcessMember Member;
        if (!P4ResolveRecoveredPostProcessMember(Storage, FName(*Name), Member) || !Member.Property ||
            MemberPlace < 0 || MemberPlace >= Member.Property->ArrayDim)
        {
            if (Writer.Lenient)
            {
                Writer.NotHeld.Add(
                    ChildPath + TEXT(" = ") + UTF8_TO_TCHAR(P4MapKit::DecodedJson(Child.second).c_str()));
                continue;
            }
            return Refuse(TEXT("level-import-struct-member-not-in-this-engine: ") + Name);
        }
        void* Data = P4RecoveredPostProcessMemberData(Target, Storage, Member);
        if (!Data)
            return Refuse(TEXT("postprocess-native-member-storage-invalid: ") + Name);
        Data = static_cast<uint8*>(Data) + MemberPlace * Member.Property->ElementSize;
        const V* Said = &Child.second;
        V NativeEnum;
        if (Member.OriginalDepthOfFieldMethod && Said->Kind == V::EKind::String)
        {
            FString NativeName;
            if (!P4RecoveredPostProcessSourceEnum(Member, UTF8_TO_TCHAR(Said->Text.c_str()), NativeName))
            {
                if (Writer.Lenient)
                {
                    Writer.NotHeld.Add(ChildPath + TEXT(" = ") + UTF8_TO_TCHAR(P4MapKit::DecodedJson(*Said).c_str()));
                    continue;
                }
                return Refuse(
                    FString(TEXT("level-import-enum-value-not-in-this-engine: ")) + UTF8_TO_TCHAR(Said->Text.c_str()));
            }
            NativeEnum = *Said;
            NativeEnum.Text = TCHAR_TO_UTF8(*NativeName);
            Said = &NativeEnum;
        }
        if (!Writer.Value(Member.Property, Data, *Said, ChildPath, 1))
            return EP4RecoveredPostProcessWrite::Refused;
    }
    return EP4RecoveredPostProcessWrite::Applied;
}

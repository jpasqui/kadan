#include "P4TextureProfileCapture.h"
#include "Serialization/JsonReader.h"
#include "Serialization/BufferReader.h"
#include "Misc/DateTime.h"

namespace
{
bool Exact(const FString& A, const TCHAR* B)
{
    return A.Equals(B, ESearchCase::CaseSensitive);
}
bool White(TCHAR C)
{
    return C == ' ' || C == '\t' || C == '\r' || C == '\n';
}
bool Digit(TCHAR C)
{
    return C >= '0' && C <= '9';
}
int Hex(TCHAR C)
{
    return C >= '0' && C <= '9' ? C - '0'
        : C >= 'a' && C <= 'f'  ? C - 'a' + 10
        : C >= 'A' && C <= 'F'  ? C - 'A' + 10
                                : -1;
}
// UE4.26 StringConv replaces these valid scalar noncharacters in UTF8.
// Selected strings must survive the public JSON/UTF8 projection exactly.
bool NoNul(const FString& S)
{
    for (TCHAR C : S)
        if (!C || C == 0xfffe || C == 0xffff)
            return false;
    return true;
}
struct FExactKeys : BaseKeyFuncs<FString, FString, false>
{
    static const FString& GetSetKey(const FString& Key) { return Key; }
    static bool Matches(const FString& A, const FString& B)
    {
        // FString::Equals uses C strings, which stop at escaped JSON NUL.
        return A.Len() == B.Len() && !FMemory::Memcmp(*A, *B, SIZE_T(A.Len()) * sizeof(TCHAR));
    }
    static uint32 GetKeyHash(const FString& Key) { return GetTypeHash(Key); }
};
bool Integer(const FString& Text, int64& Out)
{
    if (Text.IsEmpty())
        return false;
    int32 At = Text[0] == '-' ? 1 : 0;
    const bool Negative = At != 0;
    if (At == Text.Len())
        return false;
    const uint64 Limit = Negative ? uint64(MAX_int64) + 1 : uint64(MAX_int64);
    uint64 Value = 0;
    for (; At < Text.Len(); ++At)
    {
        if (!Digit(Text[At]))
            return false;
        const auto D = uint64(Text[At] - '0');
        if (Value > (Limit - D) / 10)
            return false;
        Value = Value * 10 + D;
    }
    Out = Negative ? Value == uint64(MAX_int64) + 1 ? MIN_int64 : -int64(Value) : int64(Value);
    return true;
}
bool CaptureTime(const FString& S)
{
    // Original captures retain Python's microsecond precision; installed
    // ParseIso8601 rejects >3 fractional digits and accepts trailing text.
    if (S.Len() < 20 || S[4] != '-' || S[7] != '-' || S[10] != 'T' || S[13] != ':' || S[16] != ':')
        return false;
    const auto Number = [&](int32 At, int32 Count, int32& Out)
    {
        Out = 0;
        for (int32 N = 0; N < Count; ++N)
        {
            if (!Digit(S[At + N]))
                return false;
            Out = Out * 10 + S[At + N] - '0';
        }
        return true;
    };
    int32 Year, Month, Day, Hour, Minute, Second;
    if (!Number(0, 4, Year) || !Number(5, 2, Month) || !Number(8, 2, Day) || !Number(11, 2, Hour) ||
        !Number(14, 2, Minute) || !Number(17, 2, Second) ||
        !FDateTime::Validate(Year, Month, Day, Hour, Minute, Second, 0))
        return false;
    int32 At = 19;
    if (S[At] == '.')
    {
        const int32 Begin = ++At;
        while (At < S.Len() && Digit(S[At]))
            ++At;
        if (At == Begin || At - Begin > 9)
            return false;
    }
    if (At < S.Len() && S[At] == 'Z')
        return At + 1 == S.Len();
    if (S.Len() - At != 6 || (S[At] != '+' && S[At] != '-') || S[At + 3] != ':')
        return false;
    int32 OffsetHour, OffsetMinute;
    return Number(At + 1, 2, OffsetHour) && Number(At + 4, 2, OffsetMinute) && OffsetHour <= 23 && OffsetMinute <= 59;
}
// UE4.26's reader accepts uppercase short literals and raw string controls.
// Strict lexical validation precedes streaming semantic traversal; no DOM.
bool Lexical(const FString& S, const FP4TextureProfileCaptureBudget& B, const TFunction<bool()>& Cancel, FString& Error)
{
    const auto Reject = [&](const TCHAR* Code)
    {
        Error = Code;
        return false;
    };
    const auto Delimiter = [&](int32 At)
    { return At == S.Len() || White(S[At]) || S[At] == ',' || S[At] == ']' || S[At] == '}'; };
    uint64 Tokens = 0;
    TArray<TCHAR> States;
    bool Root = false;
    const auto ConsumeValue = [&]()
    {
        if (States.Num() == 0)
        {
            if (Root)
                return false;
            Root = true;
            return true;
        }
        if (States.Last() == 'v')
        {
            States.Last() = 'o';
            return true;
        }
        if (States.Last() == 'a' || States.Last() == 'A')
        {
            States.Last() = 'r';
            return true;
        }
        return false;
    };
    const auto Grammar = [&](TCHAR C)
    {
        if (C == '{' || C == '[')
        {
            if (!ConsumeValue() || uint32(States.Num()) >= B.MaxDepth)
                return false;
            States.Add(C == '{' ? 'k' : 'a');
            return true;
        }
        if (C == '}' || C == ']')
        {
            if (States.Num() == 0)
                return false;
            const TCHAR State = States.Last();
            if (C == '}' ? State != 'k' && State != 'o' : State != 'a' && State != 'r')
                return false;
            States.Pop();
            return true;
        }
        if (C == ':')
        {
            if (States.Num() == 0 || States.Last() != 'c')
                return false;
            States.Last() = 'v';
            return true;
        }
        if (C == ',')
        {
            if (States.Num() == 0)
                return false;
            if (States.Last() == 'o')
            {
                States.Last() = 'K';
                return true;
            }
            if (States.Last() == 'r')
            {
                States.Last() = 'A';
                return true;
            }
            return false;
        }
        if (C == '"' && States.Num() && (States.Last() == 'k' || States.Last() == 'K'))
        {
            States.Last() = 'c';
            return true;
        }
        return ConsumeValue();
    };
    for (int32 At = 0; At < S.Len();)
    {
        if ((At & 4095) == 0 && Cancel && Cancel())
            return Reject(TEXT("capture-cancelled"));
        const TCHAR C = S[At++];
        if (White(C))
            continue;
        if (++Tokens > B.MaxNotations * 4)
            return Reject(TEXT("capture-token-budget"));
        if ((C == '{' || C == '[') && uint32(States.Num()) >= B.MaxDepth)
            return Reject(TEXT("capture-depth-budget"));
        if (!Grammar(C))
            return Reject(TEXT("capture-json-grammar"));
        if (C == '"')
        {
            const int32 Start = At;
            bool Closed = false;
            while (At < S.Len())
            {
                if (uint64(At - Start) > B.MaxStringCharacters)
                    return Reject(TEXT("capture-string-budget"));
                if (((At - Start) & 4095) == 0 && Cancel && Cancel())
                    return Reject(TEXT("capture-cancelled"));
                const TCHAR Ch = S[At++];
                if (Ch == '"')
                {
                    Closed = true;
                    break;
                }
                if (uint32(Ch) < 0x20)
                    return Reject(TEXT("capture-raw-string-control"));
                if (Ch == '\\')
                {
                    if (At == S.Len())
                        return Reject(TEXT("capture-truncated-escape"));
                    const TCHAR Escape = S[At++];
                    if (Escape == 'u')
                    {
                        if (S.Len() - At < 4)
                            return Reject(TEXT("capture-truncated-unicode"));
                        uint32 Code = 0;
                        for (int32 N = 0; N < 4; ++N)
                        {
                            const int D = Hex(S[At++]);
                            if (D < 0)
                                return Reject(TEXT("capture-invalid-unicode"));
                            Code = (Code << 4) | uint32(D);
                        }
                        if (Code >= 0xd800 && Code <= 0xdbff)
                        {
                            if (S.Len() - At < 6 || S[At] != '\\' || S[At + 1] != 'u')
                                return Reject(TEXT("capture-unpaired-surrogate"));
                            At += 2;
                            uint32 Low = 0;
                            for (int32 N = 0; N < 4; ++N)
                            {
                                const int D = Hex(S[At++]);
                                if (D < 0)
                                    return Reject(TEXT("capture-invalid-unicode"));
                                Low = (Low << 4) | uint32(D);
                            }
                            if (Low < 0xdc00 || Low > 0xdfff)
                                return Reject(TEXT("capture-unpaired-surrogate"));
                        }
                        else if (Code >= 0xdc00 && Code <= 0xdfff)
                            return Reject(TEXT("capture-unpaired-surrogate"));
                    }
                    else if (Escape != '"' && Escape != '\\' && Escape != '/' && Escape != 'b' && Escape != 'f' &&
                        Escape != 'n' && Escape != 'r' && Escape != 't')
                        return Reject(TEXT("capture-invalid-escape"));
                }
            }
            if (!Closed)
                return Reject(TEXT("capture-truncated-string"));
        }
        else if (C == '-' || Digit(C))
        {
            --At;
            const int32 NumberStart = At;
            const auto Digits = [&]()
            {
                while (At < S.Len() && Digit(S[At]))
                {
                    if (uint64(At - NumberStart) > B.MaxStringCharacters)
                        return Reject(TEXT("capture-number-budget"));
                    if ((At & 4095) == 0 && Cancel && Cancel())
                        return Reject(TEXT("capture-cancelled"));
                    ++At;
                }
                return true;
            };
            if (S[At] == '-')
                ++At;
            if (At == S.Len() || !Digit(S[At]))
                return Reject(TEXT("capture-invalid-number"));
            if (S[At] == '0')
                ++At;
            else if (!Digits())
                return false;
            if (At < S.Len() && S[At] == '.')
            {
                ++At;
                const int32 Begin = At;
                if (!Digits())
                    return false;
                if (At == Begin)
                    return Reject(TEXT("capture-invalid-number"));
            }
            if (At < S.Len() && (S[At] == 'e' || S[At] == 'E'))
            {
                ++At;
                if (At < S.Len() && (S[At] == '+' || S[At] == '-'))
                    ++At;
                const int32 Begin = At;
                if (!Digits())
                    return false;
                if (At == Begin)
                    return Reject(TEXT("capture-invalid-number"));
            }
            if (!Delimiter(At))
                return Reject(TEXT("capture-invalid-number"));
        }
        else if (C == 't' || C == 'f' || C == 'n')
        {
            const TCHAR* Literal = C == 't' ? TEXT("true") : C == 'f' ? TEXT("false") : TEXT("null");
            const int32 Count = C == 'f' ? 5 : 4;
            --At;
            if (S.Len() - At < Count || FCString::Strncmp(*S + At, Literal, Count) != 0)
                return Reject(TEXT("capture-invalid-literal"));
            At += Count;
            if (!Delimiter(At))
                return Reject(TEXT("capture-invalid-literal"));
        }
        else if (C == ',')
        {
            int32 Next = At;
            while (Next < S.Len() && White(S[Next]))
                ++Next;
            if (Next == S.Len() || S[Next] == ']' || S[Next] == '}')
                return Reject(TEXT("capture-trailing-comma"));
        }
        else if (C != '{' && C != '}' && C != '[' && C != ']' && C != ':')
            return Reject(TEXT("capture-invalid-token"));
    }
    return Root && States.Num() == 0 ? true : Reject(TEXT("capture-truncated-json"));
}
enum class ERole
{
    Other,
    Root,
    Enums,
    Enum,
    Values,
    Entry
};
struct FEnum
{
    FString Path, Status;
    TArray<FP4CapturedTextureGroup> Entries;
    bool HasPath = false, HasStatus = false, HasValues = false, Invalid = false;
};
struct FEntry
{
    FP4CapturedTextureGroup Value;
    bool HasName = false, HasValue = false, Invalid = false;
};
struct FParser
{
    TSharedRef<TJsonReader<TCHAR>> Reader;
    const FP4TextureProfileCaptureBudget& Budget;
    const TFunction<bool()>& Cancel;
    FString& Error;
    FBufferReader& Buffer;
    const FString& Snapshot;
    FString Identifier, StringValue;
    FP4TextureProfileCapture Result;
    uint64 Tokens = 0;
    bool Schema = false, Sha = false, HasCapturedAt = false, Scope = false, Enums = false, Target = false;
    FParser(TSharedRef<TJsonReader<TCHAR>> InReader, const FP4TextureProfileCaptureBudget& InBudget,
        const TFunction<bool()>& InCancel, FString& InError, FBufferReader& InBuffer, const FString& InSnapshot)
        : Reader(InReader), Budget(InBudget), Cancel(InCancel), Error(InError), Buffer(InBuffer), Snapshot(InSnapshot)
    {}
    bool Reject(const TCHAR* Code)
    {
        Error = Code;
        return false;
    }
    bool DecodeString(int32& At, int32 End, FString& Out)
    {
        Out.Empty();
        if (At >= End || Snapshot[At++] != '"')
            return Reject(TEXT("capture-raw-string-span"));
        while (At < End)
        {
            TCHAR C = Snapshot[At++];
            if (C == '"')
                return true;
            if (C == '\\')
            {
                if (At == End)
                    return Reject(TEXT("capture-raw-string-span"));
                C = Snapshot[At++];
                switch (C)
                {
                    case 'b':
                        C = '\b';
                        break;
                    case 'f':
                        C = '\f';
                        break;
                    case 'n':
                        C = '\n';
                        break;
                    case 'r':
                        C = '\r';
                        break;
                    case 't':
                        C = '\t';
                        break;
                    case 'u':
                    {
                        if (End - At < 4)
                            return Reject(TEXT("capture-raw-string-span"));
                        uint32 Code = 0;
                        for (int32 N = 0; N < 4; ++N)
                        {
                            const int D = Hex(Snapshot[At++]);
                            if (D < 0)
                                return Reject(TEXT("capture-raw-string-span"));
                            Code = (Code << 4) | uint32(D);
                        }
                        if (sizeof(TCHAR) == 4 && Code >= 0xd800 && Code <= 0xdbff)
                        {
                            if (End - At < 6 || Snapshot[At++] != '\\' || Snapshot[At++] != 'u')
                                return Reject(TEXT("capture-raw-string-span"));
                            uint32 Low = 0;
                            for (int32 N = 0; N < 4; ++N)
                            {
                                const int D = Hex(Snapshot[At++]);
                                if (D < 0)
                                    return Reject(TEXT("capture-raw-string-span"));
                                Low = (Low << 4) | uint32(D);
                            }
                            Code = 0x10000 + ((Code - 0xd800) << 10) + (Low - 0xdc00);
                        }
                        C = TCHAR(Code);
                        break;
                    }
                    default:
                        break; // Lexical pass already validates every escape.
                }
            }
            // Unlike operator+=(TCHAR), AppendChars preserves U+0000.
            Out.AppendChars(&C, 1);
        }
        return Reject(TEXT("capture-raw-string-span"));
    }
    bool Next(EJsonNotation& N)
    {
        if (Cancel && Cancel())
            return Reject(TEXT("capture-cancelled"));
        if (++Tokens > Budget.MaxNotations)
            return Reject(TEXT("capture-notation-budget"));
        const int32 Begin = int32(Buffer.Tell() / sizeof(TCHAR));
        if (!Reader->ReadNext(N))
            return Reject(TEXT("capture-truncated-json"));
        if (N == EJsonNotation::Error)
            return Reject(TEXT("capture-json-syntax"));
        const int32 End = int32(Buffer.Tell() / sizeof(TCHAR));
        int32 At = Begin;
        Identifier.Empty();
        StringValue.Empty();
        while (At < End && (White(Snapshot[At]) || Snapshot[At] == ','))
            ++At;
        if (At < End && Snapshot[At] == '"')
        {
            FString First;
            if (!DecodeString(At, End, First))
                return false;
            while (At < End && White(Snapshot[At]))
                ++At;
            if (At < End && Snapshot[At] == ':')
            {
                Identifier = MoveTemp(First);
                ++At;
                while (At < End && White(Snapshot[At]))
                    ++At;
                if (N == EJsonNotation::String && !DecodeString(At, End, StringValue))
                    return false;
            }
            else if (N == EJsonNotation::String)
                StringValue = MoveTemp(First);
        }
        if (Identifier.Len() > int32(Budget.MaxStringCharacters) ||
            (N == EJsonNotation::String && StringValue.Len() > int32(Budget.MaxStringCharacters)))
            return Reject(TEXT("capture-string-budget"));
        return true;
    }
    bool Scalar(ERole Role, const FString& Key, EJsonNotation N, FEnum* Record, FEntry* Entry)
    {
        if (Role == ERole::Root)
        {
            if (Exact(Key, TEXT("schema_version")))
            {
                Schema = N == EJsonNotation::Number && Exact(Reader->GetValueAsNumberString(), TEXT("2"));
                if (!Schema)
                    return Reject(TEXT("capture-schema-unqualified"));
            }
            if (Exact(Key, TEXT("source_sha256")))
            {
                if (N != EJsonNotation::String)
                    return Reject(TEXT("capture-sha-unqualified"));
                Result.DeclaredSourceSha256 = StringValue;
                Sha = true;
            }
            if (Exact(Key, TEXT("captured_at")))
            {
                if (N != EJsonNotation::String)
                    return Reject(TEXT("capture-time-unqualified"));
                Result.DeclaredCapturedAt = StringValue;
                HasCapturedAt = true;
            }
            if (Exact(Key, TEXT("scope")))
            {
                if (N != EJsonNotation::String)
                    return Reject(TEXT("capture-scope-unqualified"));
                Result.DeclaredScope = StringValue;
                Scope = true;
            }
        }
        if (Role == ERole::Enum && Record)
        {
            if (Exact(Key, TEXT("path")))
            {
                Record->HasPath = N == EJsonNotation::String;
                if (Record->HasPath)
                    Record->Path = StringValue;
                else
                    Record->Invalid = true;
            }
            if (Exact(Key, TEXT("status")))
            {
                Record->HasStatus = N == EJsonNotation::String;
                if (Record->HasStatus)
                    Record->Status = StringValue;
                else
                    Record->Invalid = true;
            }
        }
        if (Role == ERole::Entry && Entry)
        {
            if (Exact(Key, TEXT("name")))
            {
                Entry->HasName = N == EJsonNotation::String;
                if (Entry->HasName)
                    Entry->Value.Name = StringValue;
                else
                    Entry->Invalid = true;
            }
            if (Exact(Key, TEXT("value")))
            {
                Entry->HasValue = N == EJsonNotation::Number &&
                    Integer(Reader->GetValueAsNumberString(), Entry->Value.Value);
                if (!Entry->HasValue)
                    Entry->Invalid = true;
            }
        }
        return true;
    }
    bool Value(EJsonNotation N, ERole Role, uint32 Depth, FEnum* Record = nullptr, FEntry* Entry = nullptr)
    {
        if (Depth > Budget.MaxDepth)
            return Reject(TEXT("capture-depth-budget"));
        if (N == EJsonNotation::ObjectStart)
        {
            TSet<FString, FExactKeys> Keys;
            for (;;)
            {
                EJsonNotation Child;
                if (!Next(Child))
                    return false;
                if (Child == EJsonNotation::ObjectEnd)
                    return true;
                if (Child == EJsonNotation::ArrayEnd)
                    return Reject(TEXT("capture-container-mismatch"));
                const FString Key = Identifier;
                if (Keys.Contains(Key))
                    return Reject(TEXT("capture-duplicate-key"));
                if (Keys.Num() >= int32(Budget.MaxObjectKeys))
                    return Reject(TEXT("capture-key-budget"));
                Keys.Add(Key);
                ERole ChildRole = ERole::Other;
                if (Role == ERole::Root && Exact(Key, TEXT("enums")))
                {
                    if (Child != EJsonNotation::ArrayStart)
                        return Reject(TEXT("capture-enums-unqualified"));
                    Enums = true;
                    ChildRole = ERole::Enums;
                }
                if (Role == ERole::Enum && Exact(Key, TEXT("values")))
                {
                    Record->HasValues = Child == EJsonNotation::ArrayStart;
                    if (!Record->HasValues)
                        Record->Invalid = true;
                    else
                        ChildRole = ERole::Values;
                }
                if (!Scalar(Role, Key, Child, Record, Entry) || !Value(Child, ChildRole, Depth + 1, Record, Entry))
                    return false;
            }
        }
        if (N == EJsonNotation::ArrayStart)
        {
            for (;;)
            {
                EJsonNotation Child;
                if (!Next(Child))
                    return false;
                if (Child == EJsonNotation::ArrayEnd)
                    return true;
                if (Child == EJsonNotation::ObjectEnd)
                    return Reject(TEXT("capture-container-mismatch"));
                if (Role == ERole::Enums)
                {
                    if (Child != EJsonNotation::ObjectStart)
                        return Reject(TEXT("capture-enum-record-unqualified"));
                    FEnum Current;
                    if (!Value(Child, ERole::Enum, Depth + 1, &Current))
                        return false;
                    if (Current.Path.Equals(TEXT("/Script/Engine.TextureGroup"), ESearchCase::IgnoreCase))
                    {
                        if (!Exact(Current.Path, TEXT("/Script/Engine.TextureGroup")))
                            return Reject(TEXT("capture-target-identity-replaced"));
                        if (Target)
                            return Reject(TEXT("capture-duplicate-target"));
                        if (Current.Invalid || !Current.HasPath || !Current.HasStatus ||
                            !Exact(Current.Status, TEXT("observed")) || !Current.HasValues ||
                            Current.Entries.Num() != 65)
                            return Reject(TEXT("capture-target-unqualified"));
                        TSet<FString> Names;
                        TSet<int64> Values;
                        for (const auto& Item : Current.Entries)
                        {
                            if (Item.Name.IsEmpty() || !NoNul(Item.Name) || Item.Name.Len() >= NAME_SIZE ||
                                Names.Contains(Item.Name) || Values.Contains(Item.Value))
                                return Reject(TEXT("capture-entry-identity-replaced"));
                            Names.Add(Item.Name);
                            Values.Add(Item.Value);
                        }
                        Result.Entries = MoveTemp(Current.Entries);
                        Target = true;
                    }
                }
                else if (Role == ERole::Values)
                {
                    if (uint32(Record->Entries.Num()) >= Budget.MaxEnumEntries)
                        return Reject(TEXT("capture-enum-entry-budget"));
                    FEntry Current;
                    if (Child != EJsonNotation::ObjectStart)
                        Record->Invalid = true;
                    if (!Value(Child, Child == EJsonNotation::ObjectStart ? ERole::Entry : ERole::Other, Depth + 1,
                            Record, &Current))
                        return false;
                    if (!Current.HasName || !Current.HasValue || Current.Invalid)
                        Record->Invalid = true;
                    Record->Entries.Add(MoveTemp(Current.Value));
                }
                else if (!Value(Child, ERole::Other, Depth + 1, Record, Entry))
                    return false;
            }
        }
        return N == EJsonNotation::String || N == EJsonNotation::Number || N == EJsonNotation::Boolean ||
            N == EJsonNotation::Null || Reject(TEXT("capture-unexpected-notation"));
    }
};
} //namespace

bool P4ReadTextureProfileCapture(const FString& Snapshot, const FP4TextureProfileCaptureBudget& Budget,
    const TFunction<bool()>& Cancel, FP4TextureProfileCapture& Out, FString& Error)
{
    Out = {};
    Error.Empty();
    const auto Reject = [&](const TCHAR* Code)
    {
        Error = Code;
        return false;
    };
    if (Snapshot.IsEmpty() || uint64(Snapshot.Len()) > Budget.MaxCharacters || !Budget.MaxNotations ||
        Budget.MaxNotations > MAX_uint64 / 4 || !Budget.MaxDepth || Budget.MaxDepth > 256 || !Budget.MaxObjectKeys ||
        Budget.MaxObjectKeys > MAX_int32 || !Budget.MaxStringCharacters || Budget.MaxStringCharacters > MAX_int32 ||
        !Budget.MaxEnumEntries || Budget.MaxEnumEntries > MAX_int32)
        return Reject(TEXT("capture-input-budget"));
    if (!Lexical(Snapshot, Budget, Cancel, Error))
        return false;
    FBufferReader Buffer((void*)*Snapshot, int64(Snapshot.Len()) * sizeof(TCHAR), false);
    FParser Parser(TJsonReader<TCHAR>::Create(&Buffer), Budget, Cancel, Error, Buffer, Snapshot);
    EJsonNotation N;
    if (!Parser.Next(N))
        return false;
    if (N != EJsonNotation::ObjectStart)
        return Reject(TEXT("capture-root-unqualified"));
    if (!Parser.Value(N, ERole::Root, 1))
        return false;
    if (Cancel && Cancel())
        return Reject(TEXT("capture-cancelled"));
    if (Parser.Reader->ReadNext(N) || !Parser.Reader->GetErrorMessage().IsEmpty() || !Buffer.AtEnd())
        return Reject(TEXT("capture-trailing-or-malformed-json"));
    if (!Parser.Schema || !Parser.Sha || !Parser.HasCapturedAt || !Parser.Scope || !Parser.Enums || !Parser.Target)
        return Reject(TEXT("capture-required-evidence-missing"));
    if (!NoNul(Parser.Result.DeclaredSourceSha256) || !NoNul(Parser.Result.DeclaredCapturedAt) ||
        !NoNul(Parser.Result.DeclaredScope))
        return Reject(TEXT("capture-metadata-embedded-nul"));
    if (Parser.Result.DeclaredSourceSha256.Len() != 64)
        return Reject(TEXT("capture-sha-unqualified"));
    for (TCHAR C : Parser.Result.DeclaredSourceSha256)
        if (Hex(C) < 0)
            return Reject(TEXT("capture-sha-unqualified"));
    if (!CaptureTime(Parser.Result.DeclaredCapturedAt))
        return Reject(TEXT("capture-time-unqualified"));
    if (!Parser.Result.DeclaredScope.Contains(TEXT("non-atomic"), ESearchCase::IgnoreCase) ||
        !Parser.Result.DeclaredScope.Contains(TEXT("only loaded"), ESearchCase::IgnoreCase))
        return Reject(TEXT("capture-scope-unqualified"));
    Out = MoveTemp(Parser.Result);
    return true;
}

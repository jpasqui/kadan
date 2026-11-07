#include "P4BlenderData.h"
#include "Internationalization/BreakIterator.h"
#include "Internationalization/IBreakIterator.h"
#include "Internationalization/Regex.h"
#include "Misc/Compression.h"
#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/PropertyAccessUtil.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include <cstdio>
#include <cstdlib>
#include <string>

// ----- Python's JSON ------------------------------------------------------------------
FP4PyJson* FP4PyJson::Find(const FString& Key)
{
    for (auto& Field : Fields)
        if (Field.first.Equals(Key, ESearchCase::CaseSensitive))
            return &Field.second;
    return nullptr;
}
const FP4PyJson* FP4PyJson::Find(const FString& Key) const
{
    for (const auto& Field : Fields)
        if (Field.first.Equals(Key, ESearchCase::CaseSensitive))
            return &Field.second;
    return nullptr;
}
FP4PyJson& FP4PyJson::Set(const FString& Key, FP4PyJson Value)
{
    if (FP4PyJson* Existing = Find(Key))
    {
        *Existing = MoveTemp(Value);
        return *Existing;
    }
    Fields.emplace_back(Key, MoveTemp(Value));
    return Fields.back().second;
}
bool FP4PyJson::Remove(const FString& Key)
{
    for (auto It = Fields.begin(); It != Fields.end(); ++It)
        if (It->first.Equals(Key, ESearchCase::CaseSensitive))
        {
            Fields.erase(It);
            return true;
        }
    return false;
}

namespace
{
void Quote(const FString& Text, FString& Out)
{
    Out += TEXT('"');
    for (TCHAR C : Text)
    {
        switch (C)
        {
            case TEXT('"'):
                Out += TEXT("\\\"");
                break;
            case TEXT('\\'):
                Out += TEXT("\\\\");
                break;
            case TEXT('\n'):
                Out += TEXT("\\n");
                break;
            case TEXT('\r'):
                Out += TEXT("\\r");
                break;
            case TEXT('\t'):
                Out += TEXT("\\t");
                break;
            case TEXT('\b'):
                Out += TEXT("\\b");
                break;
            case TEXT('\f'):
                Out += TEXT("\\f");
                break;
            default:
                if (C < 0x20 || C > 0x7e)
                    Out += FString::Printf(TEXT("\\u%04x"), (uint32)(uint16)C);
                else
                    Out += C;
        }
    }
    Out += TEXT('"');
}

void DumpTo(const FP4PyJson& Value, FString& Out, int32 Level)
{
    using EKind = FP4PyJson::EKind;
    switch (Value.Kind)
    {
        case EKind::Null:
            Out += TEXT("null");
            return;
        case EKind::Bool:
            Out += Value.Bool ? TEXT("true") : TEXT("false");
            return;
        case EKind::Int:
            Out += FString::Printf(TEXT("%lld"), (long long)Value.Int);
            return;
        case EKind::Float:
            Out += P4Blender::PythonFloat(Value.Float);
            return;
        case EKind::String:
            Quote(Value.String, Out);
            return;
        case EKind::List:
            if (Value.Items.empty())
            {
                Out += TEXT("[]");
                return;
            }
            Out += TEXT("[\n");
            for (size_t I = 0; I < Value.Items.size(); ++I)
            {
                Out += FString::ChrN(Level + 1, TEXT(' '));
                DumpTo(Value.Items[I], Out, Level + 1);
                Out += I + 1 < Value.Items.size() ? TEXT(",\n") : TEXT("\n");
            }
            Out += FString::ChrN(Level, TEXT(' ')) + TEXT("]");
            return;
        case EKind::Dict:
            if (Value.Fields.empty())
            {
                Out += TEXT("{}");
                return;
            }
            Out += TEXT("{\n");
            for (size_t I = 0; I < Value.Fields.size(); ++I)
            {
                Out += FString::ChrN(Level + 1, TEXT(' '));
                Quote(Value.Fields[I].first, Out);
                Out += TEXT(": ");
                DumpTo(Value.Fields[I].second, Out, Level + 1);
                Out += I + 1 < Value.Fields.size() ? TEXT(",\n") : TEXT("\n");
            }
            Out += FString::ChrN(Level, TEXT(' ')) + TEXT("}");
            return;
    }
}

// str.isspace() of Python.
bool PySpace(TCHAR C)
{
    return (C >= 0x09 && C <= 0x0d) || (C >= 0x1c && C <= 0x20) || C == 0x85 || C == 0xa0 || C == 0x1680 ||
        (C >= 0x2000 && C <= 0x200a) || C == 0x2028 || C == 0x2029 || C == 0x202f || C == 0x205f || C == 0x3000;
}

FString PyStrip(const FString& Text)
{
    int32 Begin = 0, End = Text.Len();
    while (Begin < End && PySpace(Text[Begin]))
        ++Begin;
    while (End > Begin && PySpace(Text[End - 1]))
        --End;
    return Text.Mid(Begin, End - Begin);
}

FString StripChar(const FString& Text, TCHAR C)
{
    int32 Begin = 0, End = Text.Len();
    while (Begin < End && Text[Begin] == C)
        ++Begin;
    while (End > Begin && Text[End - 1] == C)
        --End;
    return Text.Mid(Begin, End - Begin);
}

// str.splitlines() of Python.
TArray<FString> SplitLines(const FString& Text)
{
    TArray<FString> Lines;
    FString Current;
    for (int32 I = 0; I < Text.Len(); ++I)
    {
        const TCHAR C = Text[I];
        const bool Break = C == '\n' || C == '\r' || C == 0x0b || C == 0x0c || C == 0x1c || C == 0x1d || C == 0x1e ||
            C == 0x85 || C == 0x2028 || C == 0x2029;
        if (!Break)
        {
            Current += C;
            continue;
        }
        if (C == '\r' && I + 1 < Text.Len() && Text[I + 1] == '\n')
            ++I;
        Lines.Add(MoveTemp(Current));
        Current.Reset();
    }
    if (!Current.IsEmpty())
        Lines.Add(MoveTemp(Current));
    return Lines;
}

// A regular expression match as re.match (anchored with ^ in the pattern) or re.search.
struct FMatch
{
    bool Ok = false;
    TArray<FString> Groups;
    TArray<bool> Has;
    const FString& operator[](int32 I) const { return Groups[I]; }
};

FMatch Match(const FRegexPattern& Pattern, const FString& Text, int32 Count)
{
    FMatch Out;
    FRegexMatcher Matcher(Pattern, Text);
    if (!Matcher.FindNext())
        return Out;
    Out.Ok = true;
    for (int32 I = 0; I <= Count; ++I)
    {
        const bool Present = Matcher.GetCaptureGroupBeginning(I) != INDEX_NONE;
        Out.Has.Add(Present);
        Out.Groups.Add(Present ? Matcher.GetCaptureGroup(I) : FString());
    }
    return Out;
}

const FRegexPattern& BeginPattern()
{
    static const FRegexPattern P(TEXT("^Begin Object(?: Class=[\\w/.]*\\.(\\w+))? Name=\"([^\"]+)\""));
    return P;
}
const FRegexPattern& LinkPattern()
{
    static const FRegexPattern P(TEXT("Expression=[\\w/.]*'\"?(?:[\\w/.]*[:.])?([\\w]+)\"?'"));
    return P;
}
const FRegexPattern& PropPattern()
{
    static const FRegexPattern P(TEXT("^\\s*(\\w+)(?:[\\(\\[](\\d+)[\\)\\]])?=(.*)$"));
    return P;
}
const FRegexPattern& InputPattern()
{
    static const FRegexPattern P(TEXT("^(\\w+)(?:[\\(\\[](\\d+)[\\)\\]])?=(\\(.*\\))$"));
    return P;
}
const FRegexPattern& PinPattern()
{
    static const FRegexPattern P(TEXT("^(\\w+)=(\\(.*\\))$"));
    return P;
}

// material_t3d._split_top
TArray<FString> SplitTop(const FString& Text)
{
    TArray<FString> Out;
    int32 Depth = 0;
    FString Current;
    for (TCHAR C : Text)
    {
        if (C == '(' || C == '[')
            ++Depth;
        else if (C == ')' || C == ']')
            --Depth;
        if (C == ',' && Depth == 0)
        {
            Out.Add(Current);
            Current.Reset();
        }
        else
            Current += C;
    }
    if (!Current.IsEmpty())
        Out.Add(Current);
    return Out;
}

// material_t3d._fields: a dict in insertion order (a later key replaces the value in place).
TArray<TPair<FString, FString>> Fields(FString Value)
{
    Value = PyStrip(Value);
    if (Value.StartsWith(TEXT("(")) && Value.EndsWith(TEXT(")")))
        Value = Value.Mid(1, Value.Len() - 2);
    TArray<TPair<FString, FString>> Out;
    for (const FString& Part : SplitTop(Value))
    {
        int32 Equal;
        if (!Part.FindChar('=', Equal))
            continue;
        const FString Key = PyStrip(Part.Left(Equal)), Rest = PyStrip(Part.Mid(Equal + 1));
        bool Found = false;
        for (auto& Pair : Out)
            if (Pair.Key.Equals(Key, ESearchCase::CaseSensitive))
            {
                Pair.Value = Rest;
                Found = true;
            }
        if (!Found)
            Out.Emplace(Key, Rest);
    }
    return Out;
}

const FString* Field(const TArray<TPair<FString, FString>>& Map, const TCHAR* Key)
{
    for (const auto& Pair : Map)
        if (Pair.Key.Equals(Key, ESearchCase::CaseSensitive))
            return &Pair.Value;
    return nullptr;
}

FString FieldOr(const TArray<TPair<FString, FString>>& Map, const TCHAR* Key, const TCHAR* Default = TEXT(""))
{
    const FString* Value = Field(Map, Key);
    return Value ? *Value : FString(Default);
}

// int() of Python on a string, false on ValueError.
bool PythonParseInt(const FString& Text, int64& Value)
{
    const FString S = PyStrip(Text);
    int32 I = 0;
    bool Negative = false;
    if (I < S.Len() && (S[I] == '+' || S[I] == '-'))
        Negative = S[I++] == '-';
    if (I >= S.Len())
        return false;
    int64 Result = 0;
    bool LastDigit = false;
    for (; I < S.Len(); ++I)
    {
        if (S[I] == '_' && LastDigit && I + 1 < S.Len() && FChar::IsDigit(S[I + 1]))
        {
            LastDigit = false;
            continue;
        }
        if (!FChar::IsDigit(S[I]))
            return false;
        Result = Result * 10 + (S[I] - '0');
        LastDigit = true;
    }
    Value = Negative ? -Result : Result;
    return true;
}

FP4PyJson NumberOrText(const FString& Text)
{
    double Value;
    return P4Blender::PythonParseFloat(Text, Value) ? FP4PyJson::Number(Value) : FP4PyJson::Text(Text);
}

// material_t3d._vector
FP4PyJson Vector(const FString& Text)
{
    const auto Map = Fields(Text);
    for (const TCHAR* Names : {TEXT("RGBA"), TEXT("XYZW")})
    {
        if (Map.Num() == 0)
            continue;
        bool All = true;
        for (const auto& Pair : Map)
            All &= Pair.Key.Len() == 1 && FCString::Strchr(Names, Pair.Key[0]) != nullptr;
        if (!All)
            continue;
        FP4PyJson Out = FP4PyJson::List();
        for (int32 I = 0; I < 4; ++I)
        {
            const FString Key = FString::Chr(Names[I]);
            const FString* Value = Field(Map, *Key);
            double Number = 0.0;
            if (Value && !P4Blender::PythonParseFloat(*Value, Number))
                return FP4PyJson();
            Out.Add(FP4PyJson::Number(Number));
        }
        return Out;
    }
    return FP4PyJson();
}

bool IsText(const FString& Name)
{
    static const TSet<FString> Text = {TEXT("ParameterName"), TEXT("InputName"), TEXT("OutputName"), TEXT("Id"),
        TEXT("ExpressionGUID"), TEXT("MaterialExpressionGuid"), TEXT("Group"), TEXT("Desc"), TEXT("Description"),
        TEXT("Text")};
    for (const FString& Item : Text)
        if (Item.Equals(Name, ESearchCase::CaseSensitive))
            return true;
    return false;
}

// material_t3d._props
FP4PyJson Props(const TArray<FString>& Block)
{
    FP4PyJson Out = FP4PyJson::Dict();
    for (const FString& Line : Block)
    {
        const FMatch M = Match(PropPattern(), Line, 3);
        if (!M.Ok)
            continue;
        const FString Name = M[1], Value = PyStrip(M[3]);
        FP4PyJson Data;
        if (Value.StartsWith(TEXT("(")))
        {
            if (Value.Contains(TEXT("Expression="), ESearchCase::CaseSensitive))
                continue;
            Data = Vector(Value);
            if (Data.IsNull())
                continue;
        }
        else if (IsText(Name))
            Data = FP4PyJson::Text(StripChar(Value, '"'));
        else
            Data = NumberOrText(StripChar(Value, '"'));
        Out.Set(M.Has[2] ? Name + M[2] : Name, MoveTemp(Data));
    }
    return Out;
}

FString Unquote(const FString& Value)
{
    return StripChar(PyStrip(Value), '"');
}

// material_t3d._expression
FP4PyJson Expression(const FString& Class, const TArray<FString>& Block)
{
    FP4PyJson Inputs = FP4PyJson::Dict();
    TMap<int64, FP4PyJson> Declared[2];
    for (const FString& Line : Block)
    {
        const FMatch M = Match(InputPattern(), Line, 3);
        if (!M.Ok)
            continue;
        const FString Name = M[1], Value = M[3];
        int64 Index = 0;
        const bool Indexed = M.Has[2] && PythonParseInt(M[2], Index);
        if (Name.Equals(TEXT("FunctionInputs"), ESearchCase::CaseSensitive) && M.Has[2])
        {
            const auto F = Fields(Value);
            const FString Inner = FieldOr(F, TEXT("Input"));
            FP4PyJson Entry = FP4PyJson::List();
            Entry.Add(FP4PyJson::Text(Unquote(FieldOr(Fields(Inner), TEXT("InputName")))));
            Entry.Add(FP4PyJson::Text(Unquote(FieldOr(F, TEXT("ExpressionInputId")))));
            if (Indexed)
                Declared[0].Add(Index, MoveTemp(Entry));
            FP4PyJson Link = P4Blender::ParseLink(Inner);
            if (!Link.IsNull())
                Inputs.Set(TEXT("FunctionInputs") + M[2], MoveTemp(Link));
        }
        else if (Name.Equals(TEXT("FunctionOutputs"), ESearchCase::CaseSensitive) && M.Has[2])
        {
            const auto F = Fields(Value);
            FP4PyJson Entry = FP4PyJson::List();
            Entry.Add(FP4PyJson::Text(Unquote(FieldOr(Fields(FieldOr(F, TEXT("Output"))), TEXT("OutputName")))));
            Entry.Add(FP4PyJson::Text(Unquote(FieldOr(F, TEXT("ExpressionOutputId")))));
            if (Indexed)
                Declared[1].Add(Index, MoveTemp(Entry));
        }
        else if (Value.Contains(TEXT("Expression="), ESearchCase::CaseSensitive))
        {
            FP4PyJson Link = P4Blender::ParseLink(Value);
            if (!Link.IsNull())
                Inputs.Set(M.Has[2] ? Name + M[2] : Name, MoveTemp(Link));
        }
    }
    FP4PyJson Node = FP4PyJson::Dict();
    Node.Set(TEXT("type"), FP4PyJson::Text(Class.Mid(FCString::Strlen(TEXT("MaterialExpression")))));
    Node.Set(TEXT("inputs"), MoveTemp(Inputs));
    FP4PyJson& NodeProps = Node.Set(TEXT("props"), Props(Block));
    const TCHAR* Names[2] = {TEXT("FunctionInputs"), TEXT("FunctionOutputs")};
    for (int32 K = 0; K < 2; ++K)
    {
        if (Declared[K].Num() == 0)
            continue;
        int64 Max = 0;
        for (const auto& Pair : Declared[K])
            Max = FMath::Max(Max, Pair.Key);
        FP4PyJson List = FP4PyJson::List();
        for (int64 I = 0; I <= Max; ++I)
        {
            if (const FP4PyJson* Entry = Declared[K].Find(I))
                List.Add(*Entry);
            else
            {
                FP4PyJson Empty = FP4PyJson::List();
                Empty.Add(FP4PyJson::Text(TEXT("")));
                Empty.Add(FP4PyJson::Text(TEXT("")));
                List.Add(MoveTemp(Empty));
            }
        }
        NodeProps.Set(Names[K], MoveTemp(List));
    }
    return Node;
}

const TCHAR* const Pins[] = {TEXT("EmissiveColor"), TEXT("BaseColor"), TEXT("Roughness"), TEXT("Metallic"),
    TEXT("Specular"), TEXT("Normal"), TEXT("Opacity"), TEXT("OpacityMask")};
} //namespace

FString FP4PyJson::Dump() const
{
    FString Out;
    DumpTo(*this, Out, 0);
    return Out;
}

namespace P4Blender
{
FString PythonFloat(double Value)
{
    if (FMath::IsNaN(Value))
        return TEXT("NaN");
    if (!FMath::IsFinite(Value))
        return Value > 0 ? TEXT("Infinity") : TEXT("-Infinity");
    char Buffer[64];
    for (int Precision = 1; Precision <= 17; ++Precision)
    {
        std::snprintf(Buffer, sizeof(Buffer), "%.*e", Precision - 1, Value);
        if (std::strtod(Buffer, nullptr) == Value)
            break;
    }
    std::string Text(Buffer);
    const bool Negative = Text[0] == '-';
    if (Negative)
        Text.erase(0, 1);
    const size_t E = Text.find('e');
    const int Exponent = std::atoi(Text.c_str() + E + 1);
    std::string Digits;
    for (size_t I = 0; I < E; ++I)
        if (Text[I] != '.')
            Digits += Text[I];
    while (Digits.size() > 1 && Digits.back() == '0')
        Digits.pop_back();
    std::string Out;
    if (Exponent >= -4 && Exponent < 16)
    {
        if (Exponent >= 0)
        {
            const size_t Whole = (size_t)Exponent + 1;
            std::string Int = Digits.substr(0, FMath::Min(Digits.size(), Whole));
            while (Int.size() < Whole)
                Int += '0';
            const std::string Fraction = Digits.size() > Whole ? Digits.substr(Whole) : std::string("0");
            Out = Int + "." + Fraction;
        }
        else
            Out = "0." + std::string((size_t)(-Exponent - 1), '0') + Digits;
    }
    else
    {
        Out = Digits.substr(0, 1);
        if (Digits.size() > 1)
            Out += "." + Digits.substr(1);
        char Suffix[16];
        std::snprintf(Suffix, sizeof(Suffix), "e%c%02d", Exponent < 0 ? '-' : '+', Exponent < 0 ? -Exponent : Exponent);
        Out += Suffix;
    }
    return FString(UTF8_TO_TCHAR((Negative ? "-" + Out : Out).c_str()));
}

bool PythonParseFloat(const FString& Text, double& Value)
{
    const FString S = PyStrip(Text);
    int32 I = 0;
    bool Negative = false;
    if (I < S.Len() && (S[I] == '+' || S[I] == '-'))
        Negative = S[I++] == '-';
    const FString Rest = S.Mid(I).ToLower();
    if (Rest == TEXT("inf") || Rest == TEXT("infinity"))
    {
        Value = Negative ? -INFINITY : INFINITY;
        return true;
    }
    if (Rest == TEXT("nan"))
    {
        Value = NAN;
        return true;
    }
    // digits with single underscores between them, an optional fraction and exponent
    std::string Clean = Negative ? "-" : "";
    auto Digits = [&](int32& At)
    {
        const int32 Start = At;
        while (At < S.Len())
        {
            if (FChar::IsDigit(S[At]) && S[At] < 128)
            {
                Clean += (char)S[At++];
                continue;
            }
            if (S[At] == '_' && At > Start && FChar::IsDigit(S[At - 1]) && At + 1 < S.Len() &&
                FChar::IsDigit(S[At + 1]))
            {
                ++At;
                continue;
            }
            break;
        }
        return At > Start;
    };
    const bool Whole = Digits(I);
    bool Fraction = false;
    if (I < S.Len() && S[I] == '.')
    {
        Clean += '.';
        ++I;
        Fraction = Digits(I);
    }
    if (!Whole && !Fraction)
        return false;
    if (I < S.Len() && (S[I] == 'e' || S[I] == 'E'))
    {
        Clean += 'e';
        ++I;
        if (I < S.Len() && (S[I] == '+' || S[I] == '-'))
            Clean += (char)S[I++];
        if (!Digits(I))
            return false;
    }
    if (I != S.Len())
        return false;
    Value = std::strtod(Clean.c_str(), nullptr);
    return true;
}

FP4PyJson ParseLink(const FString& Value)
{
    const FMatch M = Match(LinkPattern(), Value, 1);
    if (!M.Ok)
        return FP4PyJson();
    const auto F = Fields(Value);
    FString Channels;
    if (FieldOr(F, TEXT("Mask")) == TEXT("1"))
        for (const TCHAR* C : {TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A")})
            if (FieldOr(F, *(FString(TEXT("Mask")) + C)) == TEXT("1"))
                Channels += C;
    FP4PyJson Link = FP4PyJson::List();
    Link.Add(FP4PyJson::Text(M[1]));
    Link.Add(FP4PyJson::Text(Channels));
    int64 Index = 0;
    if (!PythonParseInt(FieldOr(F, TEXT("OutputIndex"), TEXT("0")), Index))
        Index = 0;
    if (Index > 0)
        Link.Add(FP4PyJson::Integer(Index));
    return Link;
}

TMap<FString, TArray<FString>> TextBlocks(const FString& Text)
{
    TMap<FString, TArray<FString>> Blocks;
    TArray<FString> Stack;
    for (const FString& Raw : SplitLines(Text))
    {
        const FString S = PyStrip(Raw);
        const FMatch M = Match(BeginPattern(), S, 2);
        if (M.Ok)
        {
            Stack.Add(M[2]);
            Blocks.FindOrAdd(M[2]);
        }
        else if (S == TEXT("End Object"))
        {
            if (Stack.Num())
                Stack.Pop();
        }
        else if (Stack.Num())
            Blocks.FindOrAdd(Stack.Last()).Add(S);
    }
    return Blocks;
}

FP4PyJson ParseMaterialT3D(const FString& Text)
{
    TArray<TPair<FString, FString>> Classes;
    TMap<FString, TArray<FString>> LinesOf;
    TArray<FString> Stack;
    for (const FString& Raw : SplitLines(Text))
    {
        const FString S = PyStrip(Raw);
        const FMatch M = Match(BeginPattern(), S, 2);
        if (M.Ok)
        {
            if (M.Has[1])
            {
                bool Found = false;
                for (auto& Pair : Classes)
                    if (Pair.Key.Equals(M[2], ESearchCase::CaseSensitive))
                    {
                        Pair.Value = M[1];
                        Found = true;
                    }
                if (!Found)
                    Classes.Emplace(M[2], M[1]);
            }
            Stack.Add(M[2]);
            LinesOf.FindOrAdd(M[2]);
            continue;
        }
        if (S == TEXT("End Object"))
        {
            if (Stack.Num())
                Stack.Pop();
            continue;
        }
        if (Stack.Num())
            LinesOf.FindOrAdd(Stack.Last()).Add(S);
    }
    FP4PyJson Nodes = FP4PyJson::Dict(), Outputs = FP4PyJson::Dict();
    static const TArray<FString> NoLines;
    for (const auto& Pair : Classes)
    {
        const TArray<FString>* Found = LinesOf.Find(Pair.Key);
        const TArray<FString>& Block = Found ? *Found : NoLines;
        if (Pair.Value == TEXT("MaterialExpressionComment"))
            continue;
        if (Pair.Value.StartsWith(TEXT("MaterialExpression"), ESearchCase::CaseSensitive))
            Nodes.Set(Pair.Key, Expression(Pair.Value, Block));
        else if (Pair.Value == TEXT("Material"))
        {
            for (const FString& Line : Block)
            {
                const FMatch M = Match(PinPattern(), Line, 2);
                if (!M.Ok)
                    continue;
                bool IsPin = false;
                for (const TCHAR* Pin : Pins)
                    IsPin |= M[1].Equals(Pin, ESearchCase::CaseSensitive);
                if (!IsPin)
                    continue;
                FP4PyJson Link = ParseLink(M[2]);
                if (!Link.IsNull())
                    Outputs.Set(M[1], MoveTemp(Link));
            }
        }
    }
    FP4PyJson Graph = FP4PyJson::Dict();
    Graph.Set(TEXT("nodes"), MoveTemp(Nodes));
    Graph.Set(TEXT("outputs"), MoveTemp(Outputs));
    return Graph;
}

FString KitPythonName(const FString& Input)
{
    FString Name = Input;
    if (Name.Len() > 1 && Name[0] == 'b' && FChar::IsUpper(Name[1]))
        Name = Name.Mid(1);
    FString Out;
    for (int32 I = 0; I < Name.Len(); ++I)
    {
        const TCHAR C = Name[I];
        if (FChar::IsUpper(C) && I &&
            (!FChar::IsUpper(Name[I - 1]) || (I + 1 < Name.Len() && FChar::IsLower(Name[I + 1]))))
            Out += '_';
        Out += FChar::ToLower(C);
    }
    return Out;
}

FString PythonizeName(const FString& Name, bool Upper)
{
    static const TSet<FString> Keywords = {TEXT("and"), TEXT("as"), TEXT("assert"), TEXT("async"), TEXT("break"),
        TEXT("class"), TEXT("continue"), TEXT("def"), TEXT("del"), TEXT("elif"), TEXT("else"), TEXT("except"),
        TEXT("finally"), TEXT("for"), TEXT("from"), TEXT("global"), TEXT("if"), TEXT("import"), TEXT("in"), TEXT("is"),
        TEXT("lambda"), TEXT("nonlocal"), TEXT("not"), TEXT("or"), TEXT("pass"), TEXT("raise"), TEXT("return"),
        TEXT("try"), TEXT("while"), TEXT("with"), TEXT("yield"), TEXT("property")};
    static TSharedPtr<IBreakIterator> Breaks;
    if (!Breaks.IsValid())
        Breaks = FBreakIterator::CreateCamelCaseBreakIterator();
    FString Out;
    Breaks->SetString(Name);
    for (int32 Previous = 0, Next = Breaks->MoveToNext(); Next != INDEX_NONE; Next = Breaks->MoveToNext())
    {
        const int32 Before = Out.Len();
        if (Before > 0 && Out[Before - 1] != '_')
            Out += '_';
        Out.AppendChars(&Name[Previous], Next - Previous);
        while (Out.Len() > Before && Out[Out.Len() - 1] == '_')
            Out.RemoveAt(Out.Len() - 1, 1, false);
        Previous = Next;
    }
    Breaks->ClearString();
    if (Upper)
        Out.ToUpperInline();
    else
        Out.ToLowerInline();
    for (const FString& Keyword : Keywords)
        if (Keyword.Equals(Out, ESearchCase::CaseSensitive))
        {
            Out += '_';
            break;
        }
    return Out;
}

namespace
{
FString ScriptName(const FString& Value)
{
    int32 Semicolon;
    return Value.FindChar(';', Semicolon) ? Value.Left(Semicolon) : Value;
}
} //namespace

FString PropertyPythonName(const FProperty* Property)
{
    FString Name = ScriptName(Property->GetMetaData(TEXT("ScriptName")));
    if (Name.IsEmpty())
        Name = Property->GetName();
    int32 Offset = 0;
    for (;;)
    {
        if (Name.Len() - Offset >= 2 && Name[Offset] == 'b' && FChar::IsUpper(Name[Offset + 1]))
        {
            Offset += 1;
            continue;
        }
        if (Name.Len() - Offset >= 3 && Name[Offset] == 'I' && Name[Offset + 1] == 'n' &&
            FChar::IsUpper(Name[Offset + 2]))
        {
            Offset += 2;
            continue;
        }
        break;
    }
    return PythonizeName(Offset ? Name.RightChop(Offset) : Name, false);
}

FString EnumPythonName(const UEnum* Enum)
{
    FString Name = ScriptName(Enum->GetMetaData(TEXT("ScriptName")));
    if (!Name.IsEmpty())
        return Name;
    Name = Enum->GetName();
    if (Name.Len() >= 2 && Name[0] == 'E' && FChar::IsUpper(Name[1]))
        Name.RemoveAt(0, 1, false);
    if (Name.Len() && FChar::IsDigit(Name[0]))
        Name.InsertAt(0, 'E');
    return Name;
}

FString EnumEntryPythonName(const UEnum* Enum, int32 Index)
{
    FString Name = ScriptName(Enum->GetMetaData(TEXT("ScriptName"), Index));
    if (Name.IsEmpty())
        Name = Enum->GetNameStringByIndex(Index);
    return PythonizeName(Name, true);
}

namespace
{
bool Exported(const FProperty* Property)
{
    if (Property->HasMetaData(TEXT("ScriptNoExport")))
        return false;
    const bool Deprecated = Property->HasMetaData(TEXT("DeprecatedProperty"));
    return Property->HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_BlueprintAssignable) || Deprecated ||
        (GIsEditor && Property->HasAnyPropertyFlags(CPF_Edit));
}
} //namespace

const FProperty* EditorProperty(const UClass* Class, const FString& PythonName)
{
    if (!Class)
        return nullptr;
    // The Python type of each class maps the Python names of its own exported properties. A name no
    // class maps is looked up as the property's own name (FName, so without case).
    FName Resolved(*PythonName);
    bool Mapped = false;
    for (const UStruct* Level = Class; Level && !Mapped; Level = Level->GetSuperStruct())
    {
        for (TFieldIterator<const FProperty> It(Level, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            if (!Exported(*It))
                continue;
            if (FName(*PropertyPythonName(*It)) == FName(*PythonName))
            {
                Resolved = It->GetFName();
                Mapped = true;
                break;
            }
        }
    }
    const FProperty* Property = Class->FindPropertyByName(Resolved);
    if (!Property)
        return nullptr;
    if (EnumHasAnyFlags(
            PropertyAccessUtil::CanGetPropertyValue(Property), EPropertyAccessResultFlags::PermissionDenied))
        return nullptr;
    return Property;
}

namespace
{
FP4PyJson EnumValue(const UEnum* Enum, int64 Value)
{
    if (!Enum)
        return FP4PyJson();
    // Python's enum types hold every entry but the last (the _MAX one) and the hidden ones.
    for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
    {
        if (Enum->HasMetaData(TEXT("Hidden"), Index) || Enum->GetValueByIndex(Index) != Value)
            continue;
        FP4PyJson Out = FP4PyJson::List();
        Out.Add(FP4PyJson::Text(TEXT("enum")));
        Out.Add(FP4PyJson::Text(EnumPythonName(Enum)));
        Out.Add(FP4PyJson::Text(EnumEntryPythonName(Enum, Index)));
        return Out;
    }
    return FP4PyJson();
}

FP4PyJson Floats(const TCHAR* Tag, std::initializer_list<double> Values)
{
    FP4PyJson Out = FP4PyJson::List();
    Out.Add(FP4PyJson::Text(Tag));
    for (double Value : Values)
        Out.Add(FP4PyJson::Number(Value));
    return Out;
}
} //namespace

FP4PyJson EncodeProperty(const FProperty* Property, const void* Container)
{
    if (!Property || !Container || Property->ArrayDim != 1)
        return FP4PyJson();
    const void* Value = Property->ContainerPtrToValuePtr<void>(Container);
    if (const FBoolProperty* P = CastField<FBoolProperty>(Property))
        return FP4PyJson::Boolean(P->GetPropertyValue(Value));
    if (const FByteProperty* P = CastField<FByteProperty>(Property))
        return P->Enum ? EnumValue(P->Enum, P->GetPropertyValue(Value))
                       : FP4PyJson::Integer(P->GetPropertyValue(Value));
    if (const FEnumProperty* P = CastField<FEnumProperty>(Property))
        return EnumValue(P->GetEnum(),
            P->GetUnderlyingProperty() ? P->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value) : 0);
    if (const FFloatProperty* P = CastField<FFloatProperty>(Property))
        return FP4PyJson::Number(P->GetPropertyValue(Value));
    if (const FDoubleProperty* P = CastField<FDoubleProperty>(Property))
        return FP4PyJson::Number(P->GetPropertyValue(Value));
    if (const FUInt64Property* P = CastField<FUInt64Property>(Property))
        return FP4PyJson::Integer((int64)P->GetPropertyValue(Value));
    if (const FNumericProperty* P = CastField<FNumericProperty>(Property))
        return P->IsInteger() ? FP4PyJson::Integer(P->GetSignedIntPropertyValue(Value)) : FP4PyJson();
    if (const FStrProperty* P = CastField<FStrProperty>(Property))
        return FP4PyJson::Text(P->GetPropertyValue(Value));
    if (const FNameProperty* P = CastField<FNameProperty>(Property))
        return FP4PyJson::Text(P->GetPropertyValue(Value).ToString());
    if (const FObjectPropertyBase* P = CastField<FObjectPropertyBase>(Property))
    {
        UObject* Object = P->LoadObjectPropertyValue(Value);
        if (!Object)
            return FP4PyJson();
        FP4PyJson Out = FP4PyJson::List();
        Out.Add(FP4PyJson::Text(TEXT("asset")));
        Out.Add(FP4PyJson::Text(Object->GetPathName()));
        return Out;
    }
    if (const FInterfaceProperty* P = CastField<FInterfaceProperty>(Property))
    {
        UObject* Object = P->GetPropertyValue(Value).GetObject();
        if (!Object)
            return FP4PyJson();
        FP4PyJson Out = FP4PyJson::List();
        Out.Add(FP4PyJson::Text(TEXT("asset")));
        Out.Add(FP4PyJson::Text(Object->GetPathName()));
        return Out;
    }
    if (const FStructProperty* P = CastField<FStructProperty>(Property))
    {
        if (P->Struct == TBaseStructure<FLinearColor>::Get())
        {
            const FLinearColor& C = *static_cast<const FLinearColor*>(Value);
            return Floats(TEXT("color"), {C.R, C.G, C.B, C.A});
        }
        if (P->Struct == TBaseStructure<FVector>::Get())
        {
            const FVector& V = *static_cast<const FVector*>(Value);
            return Floats(TEXT("vector"), {V.X, V.Y, V.Z});
        }
        if (P->Struct == TBaseStructure<FVector2D>::Get())
        {
            const FVector2D& V = *static_cast<const FVector2D*>(Value);
            return Floats(TEXT("vector2"), {V.X, V.Y});
        }
    }
    return FP4PyJson(); // text, arrays, sets, maps, delegates and other structs have no form in the add-on
}

namespace
{
uint32 Crc32(const uint8* Data, int32 Size, uint32 Crc = 0)
{
    static uint32 Table[256];
    static bool Ready = false;
    if (!Ready)
    {
        for (uint32 I = 0; I < 256; ++I)
        {
            uint32 C = I;
            for (int K = 0; K < 8; ++K)
                C = C & 1 ? 0xedb88320u ^ (C >> 1) : C >> 1;
            Table[I] = C;
        }
        Ready = true;
    }
    Crc = ~Crc;
    for (int32 I = 0; I < Size; ++I)
        Crc = Table[(Crc ^ Data[I]) & 0xff] ^ (Crc >> 8);
    return ~Crc;
}

void Chunk(TArray<uint8>& Out, const char* Tag, const TArray<uint8>& Body)
{
    const uint32 Length = Body.Num();
    const uint8 Size[4] = {uint8(Length >> 24), uint8(Length >> 16), uint8(Length >> 8), uint8(Length)};
    Out.Append(Size, 4);
    TArray<uint8> Tagged;
    Tagged.Append(reinterpret_cast<const uint8*>(Tag), 4);
    Tagged.Append(Body);
    Out.Append(Tagged);
    const uint32 Crc = Crc32(Tagged.GetData(), Tagged.Num());
    const uint8 CrcBytes[4] = {uint8(Crc >> 24), uint8(Crc >> 16), uint8(Crc >> 8), uint8(Crc)};
    Out.Append(CrcBytes, 4);
}
} //namespace

bool TgaToPng(const TArray<uint8>& Data, TArray<uint8>& Png, FString& Error)
{
    if (Data.Num() < 18)
    {
        Error = TEXT("a TGA shorter than its header");
        return false;
    }
    const int32 IdLength = Data[0], MapType = Data[1], Kind = Data[2];
    const int32 Width = Data[12] | (Data[13] << 8), Height = Data[14] | (Data[15] << 8), Bits = Data[16],
                Descriptor = Data[17];
    if (MapType || (Kind != 2 && Kind != 3 && Kind != 10 && Kind != 11) || (Bits != 8 && Bits != 24 && Bits != 32))
    {
        Error = FString::Printf(TEXT("a TGA of type %d with %d bits"), Kind, Bits);
        return false;
    }
    const int32 Size = Bits / 8;
    const int64 Total = (int64)Width * Height * Size;
    TArray<uint8> Pixels;
    int64 At = 18 + IdLength;
    if (Kind == 10 || Kind == 11)
    {
        Pixels.Reserve(Total);
        while (Pixels.Num() < Total)
        {
            if (At >= Data.Num())
            {
                Error = TEXT("a TGA whose packets end early");
                return false;
            }
            const uint8 Head = Data[At];
            const int32 Count = (Head & 127) + 1;
            if (Head & 128)
            {
                if (At + 1 + Size > Data.Num())
                {
                    Error = TEXT("a TGA whose packets end early");
                    return false;
                }
                for (int32 I = 0; I < Count; ++I)
                    Pixels.Append(&Data[At + 1], Size);
                At += 1 + Size;
            }
            else
            {
                const int32 Length = FMath::Min<int64>(Size * Count, Data.Num() - (At + 1));
                Pixels.Append(&Data[At + 1], Length);
                At += 1 + Size * Count;
            }
        }
        Pixels.SetNum(Total);
    }
    else
    {
        if (At + Total > Data.Num())
        {
            Error = TEXT("a TGA shorter than its pixels");
            return false;
        }
        Pixels.Append(&Data[At], Total);
    }
    if (Size >= 3)
        for (int64 I = 0; I + 2 < Total; I += Size)
            Swap(Pixels[I], Pixels[I + 2]);
    const int64 Stride = (int64)Width * Size;
    TArray<uint8> Raw;
    Raw.Reserve((Stride + 1) * Height);
    for (int32 Row = 0; Row < Height; ++Row)
    {
        const int32 Source = Descriptor & 0x20 ? Row : Height - 1 - Row; // stored bottom row first unless bit 5
        Raw.Add(0);
        Raw.Append(&Pixels[Source * Stride], Stride);
    }
    int32 Bound = FCompression::CompressMemoryBound(NAME_Zlib, Raw.Num());
    TArray<uint8> Compressed;
    Compressed.SetNumUninitialized(Bound);
    if (!FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), Bound, Raw.GetData(), Raw.Num()))
    {
        Error = TEXT("zlib could not compress the image");
        return false;
    }
    Compressed.SetNum(Bound);
    const uint8 Color = Size == 1 ? 0 : Size == 3 ? 2 : 6;
    TArray<uint8> Header = {uint8(Width >> 24), uint8(Width >> 16), uint8(Width >> 8), uint8(Width),
        uint8(Height >> 24), uint8(Height >> 16), uint8(Height >> 8), uint8(Height), 8, Color, 0, 0, 0};
    static const uint8 Signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    Png.Reset();
    Png.Append(Signature, 8);
    Chunk(Png, "IHDR", Header);
    Chunk(Png, "IDAT", Compressed);
    Chunk(Png, "IEND", TArray<uint8>());
    return true;
}
} //namespace P4Blender

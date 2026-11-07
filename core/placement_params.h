#pragma once
// The per-placement blobs of a placement row (FActorGenerator.ActorSerializeParameter): read and written
// field by field the way the game's own serializers do it. Each blob is a hand-written native stream with
// no tags, gated by the row's version (static blobs by AssetVersion, dynamic ones by GeneratorVersion).
//
// Wire rules, little endian: bool = u32 0/1, FName = FString (i32 length with the NUL, negative for UTF-16),
// TArray = i32 count then the items, FVector = three f32, enums one byte.
//
// The layouts are data: placement_layouts.json, generated into placement_layouts.h. A slot's layout starts with
// the slot's own fields, then takes the part of each class of the actor's (or its AI component's) native class
// chain, walked from the base class down. A blob part that is not decoded is kept as bytes (Blob::Tail) and
// written back unchanged. A blob of a kind that is not covered has no layout, and its bytes are kept whole by
// the caller.
#include "placement_layouts.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace P4MapKit
{
namespace Placement
{

// AssetVersion and GeneratorVersion of a row Kadan writes.
constexpr std::uint64_t RowVersion = UINT64_C(0x20230324A);

enum class Kind : std::uint8_t
{
    Bool,
    I32,
    U32,
    F32,
    U8,
    U16,
    U64,
    Name,
    Vec,
    Rot,
    Quat,
    Array,
    Struct,
    Repeat,
    Bools,
    SoftPath
};

struct Type;
struct Field
{
    std::string Name;
    const Type* Of = nullptr;
    char Gate = 0; // '>' above Version, '<' below it, '=' equal to it, 0 always
    std::uint64_t Version = 0;
    std::string When; // a sibling read earlier that must be set for this field to be there
    bool StopIfSet = false; // when set, the rest of the blob is not decoded (kept as Tail)
    // Where a writer takes the field from when no property of the blob's owner holds it.
    bool Derived = false; // no property holds it: written as RuntimeDefault unless a rule below fills it
    std::uint64_t RuntimeDefault = 0; // a derived scalar's bits as the game starts them
    std::string CountOf; // the owner's array property whose item count this is
    // A Repeat of the world locations of the actor's first scene components with this tag, as many as its counter says.
    std::string ComponentTag;
    bool ComponentTransform = false; // the actor's component of the field's name: its transform, then its properties;
    // the field is in a layout only when the actor has that component
    bool Drops = false; // an array of drops, each with a generator number (UniqueId)
    std::string DropKey; // drops that are generators of their own: the word of their numbers' keys
    std::string DropList; // the sibling that lists those numbers
};
struct Type
{
    Kind Is = Kind::Struct;
    std::vector<Field> Members; // Struct
    const Type* Item = nullptr; // Array, Repeat, Bools
    std::string CountFrom; // Repeat: the sibling whose value is the count
};

inline bool GateOk(const Field& F, std::uint64_t V)
{
    return F.Gate == 0 ||
        (F.Gate == '>'          ? V > F.Version
                : F.Gate == '<' ? V < F.Version
                                : F.Gate == '=' && V == F.Version);
}

// A decoded value. Scalars keep their exact bits so that a blob is written back byte for byte.
struct Value
{
    Kind Is = Kind::Struct;
    std::uint64_t Bits = 0; // bool word, integer, or a float's 32 bits
    std::uint32_t Words[4] = {0, 0, 0, 0}; // Vec and Rot (three floats), Quat (four), as bits
    std::string Text; // Name: Latin-1 bytes, or UTF-16LE bytes when Form is 2
    std::uint8_t Form = 0; // Name: 0 written as length 0, 1 narrow, 2 wide
    std::vector<Value> Items;
    std::vector<std::pair<std::string, Value>> Members;

    Value* Find(const std::string& Name)
    {
        for (auto& M : Members)
            if (M.first == Name)
                return &M.second;
        return nullptr;
    }
    const Value* Find(const std::string& Name) const
    {
        for (const auto& M : Members)
            if (M.first == Name)
                return &M.second;
        return nullptr;
    }
    bool Set() const { return Bits != 0; }
    float Float() const
    {
        float F;
        std::uint32_t B = std::uint32_t(Bits);
        std::memcpy(&F, &B, 4);
        return F;
    }
    static std::uint32_t FloatBits(float F)
    {
        std::uint32_t B;
        std::memcpy(&B, &F, 4);
        return B;
    }
    void SetFloat(float F) { Bits = FloatBits(F); }
    void SetName(const std::string& Narrow)
    {
        Text = Narrow;
        Form = Narrow.empty() ? 0 : 1;
    }
};

struct Layout
{
    std::vector<Field> Fields;
    bool Known = false;
};

struct Blob
{
    Value Root; // a Struct whose members are the layout's fields
    std::vector<std::uint8_t> Tail; // bytes after what was decoded
    int Variant = 0; // which of a slot's layouts was used
    bool UnreadFields = false;
    bool Exact() const { return Tail.empty() && !UnreadFields; }
};

inline void SetDropGeneratorNumbers(Value& Drops, Value* Listed, const std::vector<std::uint64_t>& Numbers)
{
    for (std::size_t I = 0; I < Drops.Items.size(); ++I)
        if (Value* Unique = Drops.Items[I].Find("UniqueId"))
            Unique->Bits = I < Numbers.size() ? Numbers[I] : ~UINT64_C(0);
    if (!Listed)
        return;
    Listed->Items.clear();
    for (std::size_t I = 0; I < Drops.Items.size() && I < Numbers.size(); ++I)
    {
        Value Number;
        Number.Is = Kind::U64;
        Number.Bits = Numbers[I];
        Listed->Items.push_back(Number);
    }
}

// What decides a layout: the actor's native classes and its AI component's, most derived first, and the
// names of the actor's components.
struct Context
{
    std::vector<std::string> ActorChain, ComponentChain;
    std::vector<std::string> Components;
};

namespace Detail
{

// The type of a scalar kind. Composite kinds are named types of the layout data.
inline const Type* Scalar(Kind K)
{
    static const Type Types[] = {{Kind::Bool, {}, nullptr, {}}, {Kind::I32, {}, nullptr, {}},
        {Kind::U32, {}, nullptr, {}}, {Kind::F32, {}, nullptr, {}}, {Kind::U8, {}, nullptr, {}},
        {Kind::U16, {}, nullptr, {}}, {Kind::U64, {}, nullptr, {}}, {Kind::Name, {}, nullptr, {}},
        {Kind::Vec, {}, nullptr, {}}, {Kind::Rot, {}, nullptr, {}}, {Kind::Quat, {}, nullptr, {}}};
    return &Types[int(K)];
}
// The scalar and composite kinds by the names the layout data gives them.
inline Kind KindNamed(const std::string& Name)
{
    static const std::pair<const char*, Kind> Names[] = {{"bool", Kind::Bool}, {"i32", Kind::I32}, {"u32", Kind::U32},
        {"f32", Kind::F32}, {"u8", Kind::U8}, {"u16", Kind::U16}, {"u64", Kind::U64}, {"name", Kind::Name},
        {"vec", Kind::Vec}, {"rot", Kind::Rot}, {"quat", Kind::Quat}, {"array", Kind::Array}, {"struct", Kind::Struct},
        {"repeat", Kind::Repeat}, {"bools", Kind::Bools}, {"soft_path", Kind::SoftPath}};
    for (const auto& One : Names)
    {
        if (Name == One.first)
            return One.second;
    }
    return Kind::Struct;
}

// The generated layout tables as types and fields.
struct Tables
{
    struct Part
    {
        std::string Class;
        Slot Id;
        bool Replaces;
        std::vector<std::vector<Field>> Variants;
    };
    std::deque<Type> Named;
    std::map<std::string, const Type*> ByName;
    std::map<Slot, std::vector<Field>> SlotFields;
    std::map<Slot, char> SlotChains;
    std::vector<Part> Parts;

    Tables()
    {
        const auto TypeRows = Generated::TypeRows();
        for (std::size_t I = 0; I < TypeRows.Count; ++I)
        {
            Named.emplace_back();
            ByName[TypeRows.Rows[I].Name] = &Named.back();
        }
        for (std::size_t I = 0; I < TypeRows.Count; ++I)
        {
            const Generated::TypeRow& Row = TypeRows.Rows[I];
            Type& One = Named[I];
            One.Is = KindNamed(Row.Kind);
            One.Item = *Row.Item ? TypeNamed(Row.Item) : nullptr;
            One.CountFrom = Row.CountFrom;
            One.Members = FieldsAt(Row.First, Row.Count);
        }
        const auto SlotRows = Generated::SlotRows();
        for (std::size_t I = 0; I < SlotRows.Count; ++I)
        {
            const Generated::SlotRow& Row = SlotRows.Rows[I];
            SlotFields[Row.Id] = FieldsAt(Row.First, Row.Count);
            SlotChains[Row.Id] = Row.Chain;
        }
        const auto PartRows = Generated::PartRows();
        for (std::size_t I = 0; I < PartRows.Count; ++I)
        {
            const Generated::PartRow& Row = PartRows.Rows[I];
            if (Row.Variant == 0)
                Parts.push_back(Part{Row.Class, Row.Id, Row.Replaces, {}});
            Parts.back().Variants.push_back(FieldsAt(Row.First, Row.Count));
        }
    }
    Tables(const Tables&) = delete;
    Tables& operator=(const Tables&) = delete;

    const Type* TypeNamed(const std::string& Name) const
    {
        const auto Found = ByName.find(Name);
        return Found != ByName.end() ? Found->second : Scalar(KindNamed(Name));
    }
    std::vector<Field> FieldsAt(int First, int Count) const
    {
        const auto Rows = Generated::FieldRows();
        std::vector<Field> Out;
        for (int I = First; I < First + Count; ++I)
        {
            const Generated::FieldRow& Row = Rows.Rows[I];
            Field One;
            One.Name = Row.Name;
            One.Of = TypeNamed(Row.Type);
            One.Gate = Row.Gate;
            One.Version = Row.Version;
            One.When = Row.When;
            One.StopIfSet = Row.StopIfSet;
            One.Derived = Row.Derived;
            One.RuntimeDefault = Row.RuntimeDefault;
            One.CountOf = Row.CountOf;
            One.ComponentTag = Row.ComponentTag;
            One.ComponentTransform = Row.ComponentTransform;
            One.Drops = Row.Drops;
            One.DropKey = Row.DropKey;
            One.DropList = Row.DropList;
            Out.push_back(std::move(One));
        }
        return Out;
    }
    const Part* PartOf(const std::string& Class, Slot S) const
    {
        for (const Part& One : Parts)
        {
            if (One.Id == S && One.Class == Class)
                return &One;
        }
        return nullptr;
    }
};
inline const Tables& All()
{
    static const Tables One;
    return One;
}

inline bool Has(const std::vector<std::string>& Chain, const std::string& Name)
{
    for (const auto& C : Chain)
    {
        if (C == Name)
            return true;
    }
    return false;
}

struct Reader
{
    const std::uint8_t* Data;
    std::size_t Size, At = 0;
    std::uint64_t V;
    bool Take(std::size_t N, const std::uint8_t*& Out)
    {
        if (N > Size - At)
            return false;
        Out = Data + At;
        At += N;
        return true;
    }
    template <class T>
    bool Num(T& Out)
    {
        const std::uint8_t* P;
        if (!Take(sizeof(T), P))
            return false;
        Out = 0;
        for (std::size_t I = 0; I < sizeof(T); ++I)
            Out = T(Out | (T(P[I]) << (8 * I)));
        return true;
    }
};
struct Writer
{
    std::vector<std::uint8_t>& Out;
    template <class T>
    void Num(T Value)
    {
        for (std::size_t I = 0; I < sizeof(T); ++I)
            Out.push_back(std::uint8_t((std::uint64_t(Value) >> (8 * I)) & 0xFF));
    }
};

inline bool ReadValue(Reader& R, const Type& T, Value& Out, std::string& Error);
inline bool ReadMembers(Reader& R, const std::vector<Field>& Fields, Value& Out, bool& Stopped, std::string& Error)
{
    Out.Is = Kind::Struct;
    for (const auto& Field : Fields)
    {
        if (!GateOk(Field, R.V))
            continue;
        if (!Field.When.empty())
        {
            const Value* On = Out.Find(Field.When);
            if (!On || !On->Set())
                continue;
        }
        Value One;
        One.Is = Field.Of->Is;
        if (Field.Of->Is == Kind::Repeat)
        {
            const Value* Count = Out.Find(Field.Of->CountFrom);
            if (!Count)
            {
                Error = "count of " + Field.Name + " missing";
                return false;
            }
            for (std::uint64_t I = 0; I < Count->Bits; ++I)
            {
                Value Item;
                if (!ReadValue(R, *Field.Of->Item, Item, Error))
                    return false;
                One.Items.push_back(Item);
            }
        }
        else if (Field.Of->Is == Kind::Bools)
        {
            const std::size_t Left = R.Size - R.At, Count = Left >= 4 ? (Left - 4) / 4 : 0;
            for (std::size_t I = 0; I < Count; ++I)
            {
                Value Item;
                if (!ReadValue(R, *Field.Of->Item, Item, Error))
                    return false;
                One.Items.push_back(Item);
            }
        }
        else if (!ReadValue(R, *Field.Of, One, Error))
        {
            Error = Field.Name + ": " + Error;
            return false;
        }
        Out.Members.emplace_back(Field.Name, std::move(One));
        if (Field.StopIfSet && Out.Members.back().second.Set())
        {
            Stopped = true;
            return true;
        }
    }
    return true;
}
inline bool ReadValue(Reader& R, const Type& T, Value& Out, std::string& Error)
{
    Out.Is = T.Is;
    switch (T.Is)
    {
        case Kind::Bool:
        case Kind::U32:
        {
            std::uint32_t X;
            if (!R.Num(X))
                break;
            Out.Bits = X;
            return true;
        }
        case Kind::I32:
        {
            std::uint32_t X;
            if (!R.Num(X))
                break;
            Out.Bits = X;
            return true;
        }
        case Kind::F32:
        {
            std::uint32_t X;
            if (!R.Num(X))
                break;
            Out.Bits = X;
            return true;
        }
        case Kind::U8:
        {
            std::uint8_t X;
            if (!R.Num(X))
                break;
            Out.Bits = X;
            return true;
        }
        case Kind::U16:
        {
            std::uint16_t X;
            if (!R.Num(X))
                break;
            Out.Bits = X;
            return true;
        }
        case Kind::U64:
        {
            std::uint64_t X;
            if (!R.Num(X))
                break;
            Out.Bits = X;
            return true;
        }
        case Kind::Vec:
        case Kind::Rot:
        case Kind::Quat:
        {
            const int N = T.Is == Kind::Quat ? 4 : 3;
            for (int I = 0; I < N; ++I)
                if (!R.Num(Out.Words[I]))
                {
                    Error = "truncated";
                    return false;
                }
            return true;
        }
        case Kind::Name:
        {
            std::int32_t N;
            if (!R.Num(N))
                break;
            if (N == 0)
            {
                Out.Form = 0;
                Out.Text.clear();
                return true;
            }
            if (N < -4096 || N > 4096)
            {
                Error = "bad string length";
                return false;
            }
            const std::size_t Bytes = std::size_t(N < 0 ? -N : N) * (N < 0 ? 2 : 1);
            const std::uint8_t* P;
            if (!R.Take(Bytes, P))
                break;
            // Kept as the stream has it, without the terminator, which is written back as it was read.
            Out.Form = N < 0 ? 2 : 1;
            Out.Text.assign(reinterpret_cast<const char*>(P), Bytes - (N < 0 ? 2 : 1));
            return true;
        }
        case Kind::Array:
        {
            std::int32_t N;
            if (!R.Num(N))
                break;
            if (N < 0 || N > 4096)
            {
                Error = "bad array count";
                return false;
            }
            for (std::int32_t I = 0; I < N; ++I)
            {
                Value Item;
                if (!ReadValue(R, *T.Item, Item, Error))
                    return false;
                Out.Items.push_back(std::move(Item));
            }
            return true;
        }
        case Kind::Struct:
        case Kind::SoftPath:
        {
            bool Stopped = false;
            const bool Ok = ReadMembers(R, T.Members, Out, Stopped, Error);
            Out.Is = T.Is;
            return Ok;
        }
        case Kind::Repeat:
        case Kind::Bools:
            Error = "a counted run is only a member";
            return false;
    }
    if (Error.empty())
        Error = "truncated";
    return false;
}

inline bool WriteValue(Writer& W, const Type& T, const Value& In, std::uint64_t V, std::string& Error);
inline bool WriteMembers(
    Writer& W, const std::vector<Field>& Fields, const Value& In, std::uint64_t V, bool& Stopped, std::string& Error)
{
    for (const auto& Field : Fields)
    {
        if (!GateOk(Field, V))
            continue;
        if (!Field.When.empty())
        {
            const Value* On = In.Find(Field.When);
            if (!On || !On->Set())
                continue;
        }
        const Value* One = In.Find(Field.Name);
        if (!One)
        {
            Error = Field.Name + " is missing";
            return false;
        }
        if (Field.Of->Is == Kind::Repeat)
        {
            const Value* Count = In.Find(Field.Of->CountFrom);
            if (!Count || Count->Bits != One->Items.size())
            {
                Error = Field.Name + " does not have the count its counter says";
                return false;
            }
            for (const auto& Item : One->Items)
                if (!WriteValue(W, *Field.Of->Item, Item, V, Error))
                    return false;
        }
        else if (Field.Of->Is == Kind::Bools)
        {
            for (const auto& Item : One->Items)
                if (!WriteValue(W, *Field.Of->Item, Item, V, Error))
                    return false;
        }
        else if (!WriteValue(W, *Field.Of, *One, V, Error))
        {
            Error = Field.Name + ": " + Error;
            return false;
        }
        if (Field.StopIfSet && One->Set())
        {
            Stopped = true;
            return true;
        }
    }
    return true;
}
inline bool WriteValue(Writer& W, const Type& T, const Value& In, std::uint64_t V, std::string& Error)
{
    if (In.Is != T.Is)
    {
        Error = "value of another kind";
        return false;
    }
    switch (T.Is)
    {
        case Kind::Bool:
        case Kind::U32:
        case Kind::I32:
        case Kind::F32:
            W.Num(std::uint32_t(In.Bits));
            return true;
        case Kind::U8:
            W.Num(std::uint8_t(In.Bits));
            return true;
        case Kind::U16:
            W.Num(std::uint16_t(In.Bits));
            return true;
        case Kind::U64:
            W.Num(In.Bits);
            return true;
        case Kind::Vec:
        case Kind::Rot:
        case Kind::Quat:
            for (int I = 0; I < (T.Is == Kind::Quat ? 4 : 3); ++I)
                W.Num(In.Words[I]);
            return true;
        case Kind::Name:
        {
            if (In.Form == 0)
            {
                if (!In.Text.empty())
                {
                    Error = "a name of no form has text";
                    return false;
                }
                W.Num(std::int32_t(0));
                return true;
            }
            if (In.Form == 2)
            {
                if (In.Text.size() % 2 || In.Text.size() / 2 + 1 > 4096)
                {
                    Error = "bad wide name";
                    return false;
                }
                W.Num(std::int32_t(-std::int32_t(In.Text.size() / 2 + 1)));
                W.Out.insert(W.Out.end(), In.Text.begin(), In.Text.end());
                W.Num(std::uint16_t(0));
                return true;
            }
            if (In.Text.size() + 1 > 4096 || In.Text.find('\0') != std::string::npos)
            {
                Error = "bad name";
                return false;
            }
            W.Num(std::int32_t(In.Text.size() + 1));
            W.Out.insert(W.Out.end(), In.Text.begin(), In.Text.end());
            W.Out.push_back(0);
            return true;
        }
        case Kind::Array:
            if (In.Items.size() > 4096)
            {
                Error = "too many items";
                return false;
            }
            W.Num(std::int32_t(In.Items.size()));
            for (const auto& Item : In.Items)
                if (!WriteValue(W, *T.Item, Item, V, Error))
                    return false;
            return true;
        case Kind::Struct:
        case Kind::SoftPath:
        {
            bool Stopped = false;
            return WriteMembers(W, T.Members, In, V, Stopped, Error);
        }
        case Kind::Repeat:
        case Kind::Bools:
            break;
    }
    Error = "a counted run is only a member";
    return false;
}

// A value of a type with nothing set: zeros, empty names and arrays, every gated member the version has.
inline Value Empty(const Type& T, std::uint64_t V);
inline void EmptyMembers(const std::vector<Field>& Fields, std::uint64_t V, Value& Out)
{
    Out.Is = Kind::Struct;
    for (const auto& Field : Fields)
    {
        if (!GateOk(Field, V))
            continue;
        Value One;
        if (Field.Of->Is == Kind::Repeat || Field.Of->Is == Kind::Bools)
            One.Is = Field.Of->Is;
        else
            One = Empty(*Field.Of, V);
        Out.Members.emplace_back(Field.Name, std::move(One));
    }
}
inline Value Empty(const Type& T, std::uint64_t V)
{
    Value Out;
    Out.Is = T.Is;
    if (T.Is == Kind::Struct || T.Is == Kind::SoftPath)
    {
        EmptyMembers(T.Members, V, Out);
        Out.Is = T.Is;
    }
    return Out;
}

} // namespace Detail

// The layouts a slot can have for an actor, in the order they are tried. Empty: the slot is not covered.
inline std::vector<Layout> Layouts(Slot S, const Context& C)
{
    const Detail::Tables& T = Detail::All();
    std::vector<std::vector<Field>> Variants;
    const std::vector<Field>& Own = T.SlotFields.at(S);
    if (!Own.empty())
        Variants.push_back(Own);
    const char Chain = T.SlotChains.at(S);
    const std::vector<std::string>* Classes = Chain == 'a' ? &C.ActorChain : Chain == 'c' ? &C.ComponentChain : nullptr;
    if (Classes)
    {
        for (auto Class = Classes->rbegin(); Class != Classes->rend(); ++Class)
        {
            const Detail::Tables::Part* Part = T.PartOf(*Class, S);
            if (!Part)
                continue;
            if (Part->Replaces)
            {
                Variants = Part->Variants;
                continue;
            }
            for (auto& Variant : Variants)
                Variant.insert(Variant.end(), Part->Variants[0].begin(), Part->Variants[0].end());
        }
    }
    std::vector<Layout> Out;
    for (auto& Fields : Variants)
    {
        Layout L;
        L.Known = true;
        for (auto& One : Fields)
        {
            if (!One.ComponentTransform || Detail::Has(C.Components, One.Name))
                L.Fields.push_back(std::move(One));
        }
        Out.push_back(std::move(L));
    }
    return Out;
}

// Every slot with the row's name for its blob and its half, in the order a writer fills them, and the object a
// writer reads it from: the actor's component its property OwnerProperty holds when it is of one of OwnerClasses,
// else the first component of the first of OwnerClasses that has one, not of OwnerExcept. No classes: the actor.
struct SlotName
{
    const char* Row;
    bool Static;
    Slot S;
    char Chain;
    std::string OwnerProperty;
    std::vector<std::string> OwnerClasses;
    std::string OwnerExcept;
};
inline const std::vector<SlotName>& Slots()
{
    static const std::vector<SlotName> Table = []
    {
        std::vector<SlotName> Out;
        const auto Rows = Generated::SlotRows();
        for (std::size_t I = 0; I < Rows.Count; ++I)
        {
            const Generated::SlotRow& Row = Rows.Rows[I];
            SlotName One{Row.Row, Row.Static, Row.Id, Row.Chain, Row.OwnerProperty, {}, Row.OwnerExcept};
            std::string Classes = Row.OwnerClasses;
            for (std::size_t Start = 0; Start < Classes.size();)
            {
                const std::size_t End = std::min(Classes.find(',', Start), Classes.size());
                One.OwnerClasses.push_back(Classes.substr(Start, End - Start));
                Start = End + 1;
            }
            Out.push_back(std::move(One));
        }
        return Out;
    }();
    return Table;
}

// The slot a row's blob name and half are, or false when the blob is not one of these.
inline bool SlotOf(const std::string& Name, bool Static, Slot& Out)
{
    for (const auto& E : Slots())
    {
        if (Name == E.Row && Static == E.Static)
        {
            Out = E.S;
            return true;
        }
    }
    return false;
}

// Reads a blob. False only when a layout exists and the bytes do not fit it. A blob whose slot has no layout
// for this actor is refused with Error "not covered"; the caller keeps its bytes.
inline bool Decode(Slot S, const Context& C, const std::vector<std::uint8_t>& Bytes, std::uint64_t Version, Blob& Out,
    std::string& Error)
{
    const auto Candidates = Layouts(S, C);
    if (Candidates.empty())
    {
        Error = "not covered";
        return false;
    }
    std::string Last;
    for (std::size_t I = 0; I < Candidates.size(); ++I)
    {
        Detail::Reader R{Bytes.data(), Bytes.size(), 0, Version};
        Blob Try;
        Try.Variant = int(I);
        bool Stopped = false;
        std::string Why;
        const bool Read = Detail::ReadMembers(R, Candidates[I].Fields, Try.Root, Stopped, Why);
        // A layout that does not take the blob exactly is passed over while there is another to try.
        if (Read && (R.At == R.Size || I + 1 == Candidates.size()))
        {
            Try.Tail.assign(Bytes.begin() + std::ptrdiff_t(R.At), Bytes.end());
            Try.UnreadFields = Stopped;
            Out = std::move(Try);
            Error.clear();
            return true;
        }
        Last = Read ? "not exact" : Why;
    }
    Error = Last;
    return false;
}

// Writes a blob in its layout, then its Tail. The layout is the slot's Variant for the actor.
inline bool Encode(
    Slot S, const Context& C, const Blob& In, std::uint64_t Version, std::vector<std::uint8_t>& Out, std::string& Error)
{
    const auto Candidates = Layouts(S, C);
    if (In.Variant < 0 || std::size_t(In.Variant) >= Candidates.size())
    {
        Error = "not covered";
        return false;
    }
    std::vector<std::uint8_t> Bytes;
    Detail::Writer W{Bytes};
    bool Stopped = false;
    if (!Detail::WriteMembers(W, Candidates[std::size_t(In.Variant)].Fields, In.Root, Version, Stopped, Error))
        return false;
    if (!Stopped && !In.Tail.empty() && In.Variant + 1 < int(Candidates.size()))
    {
        Error = "a tail after a complete layout";
        return false;
    }
    Bytes.insert(Bytes.end(), In.Tail.begin(), In.Tail.end());
    Out.swap(Bytes);
    Error.clear();
    return true;
}

// A blob of the slot with nothing set, for a writer to fill: every member the version has, zero or empty.
inline bool Fresh(Slot S, const Context& C, std::uint64_t Version, Blob& Out, int Variant = 0)
{
    const auto Candidates = Layouts(S, C);
    if (Variant < 0 || std::size_t(Variant) >= Candidates.size())
        return false;
    Out = Blob();
    Out.Variant = Variant;
    Detail::EmptyMembers(Candidates[std::size_t(Variant)].Fields, Version, Out.Root);
    return true;
}

// A top-level field of a slot's layout, for a writer that fills members by name. False when there is none.
inline bool FieldOf(Slot S, const Context& C, const std::string& Name, Field& Out, int Variant = 0)
{
    const auto Candidates = Layouts(S, C);
    if (Variant < 0 || std::size_t(Variant) >= Candidates.size())
        return false;
    for (const auto& F : Candidates[std::size_t(Variant)].Fields)
    {
        if (F.Name == Name)
        {
            Out = F;
            return true;
        }
    }
    return false;
}
inline const Type* FieldType(Slot S, const Context& C, const std::string& Name, int Variant = 0)
{
    Field Out;
    return FieldOf(S, C, Name, Out, Variant) ? Out.Of : nullptr;
}
inline Value EmptyOf(const Type& T, std::uint64_t Version)
{
    return Detail::Empty(T, Version);
}

// ---------------------------------------------------------------- names as text

inline void AppendUtf8(std::string& Out, std::uint32_t Code)
{
    if (Code < 0x80)
        Out += char(Code);
    else if (Code < 0x800)
    {
        Out += char(0xC0 | (Code >> 6));
        Out += char(0x80 | (Code & 0x3F));
    }
    else if (Code < 0x10000)
    {
        Out += char(0xE0 | (Code >> 12));
        Out += char(0x80 | ((Code >> 6) & 0x3F));
        Out += char(0x80 | (Code & 0x3F));
    }
    else
    {
        Out += char(0xF0 | (Code >> 18));
        Out += char(0x80 | ((Code >> 12) & 0x3F));
        Out += char(0x80 | ((Code >> 6) & 0x3F));
        Out += char(0x80 | (Code & 0x3F));
    }
}
// A name's text in UTF-8.
inline std::string NameUtf8(const Value& V)
{
    std::string Out;
    if (V.Form == 2)
    {
        for (std::size_t I = 0; I + 1 < V.Text.size(); I += 2)
        {
            std::uint32_t Unit = std::uint8_t(V.Text[I]) | (std::uint32_t(std::uint8_t(V.Text[I + 1])) << 8);
            if (Unit >= 0xD800 && Unit < 0xDC00 && I + 3 < V.Text.size())
            {
                const std::uint32_t Low = std::uint8_t(V.Text[I + 2]) |
                    (std::uint32_t(std::uint8_t(V.Text[I + 3])) << 8);
                if (Low >= 0xDC00 && Low < 0xE000)
                {
                    Unit = 0x10000 + ((Unit - 0xD800) << 10) + (Low - 0xDC00);
                    I += 2;
                }
            }
            AppendUtf8(Out, Unit);
        }
    }
    else
        for (unsigned char C : V.Text)
            AppendUtf8(Out, C);
    return Out;
}
// A name from UTF-8 text: narrow when every character fits Latin-1, wide otherwise.
inline Value NameFromUtf8(const std::string& Text)
{
    std::vector<std::uint32_t> Codes;
    for (std::size_t I = 0; I < Text.size();)
    {
        const unsigned char C = std::uint8_t(Text[I]);
        std::uint32_t Code = C;
        std::size_t N = 1;
        if (C >= 0xF0 && I + 3 < Text.size())
        {
            Code = ((C & 7u) << 18) | ((std::uint8_t(Text[I + 1]) & 0x3Fu) << 12) |
                ((std::uint8_t(Text[I + 2]) & 0x3Fu) << 6) | (std::uint8_t(Text[I + 3]) & 0x3Fu);
            N = 4;
        }
        else if (C >= 0xE0 && I + 2 < Text.size())
        {
            Code = ((C & 15u) << 12) | ((std::uint8_t(Text[I + 1]) & 0x3Fu) << 6) | (std::uint8_t(Text[I + 2]) & 0x3Fu);
            N = 3;
        }
        else if (C >= 0xC0 && I + 1 < Text.size())
        {
            Code = ((C & 31u) << 6) | (std::uint8_t(Text[I + 1]) & 0x3Fu);
            N = 2;
        }
        Codes.push_back(Code);
        I += N;
    }
    Value Out;
    Out.Is = Kind::Name;
    bool Narrow = true;
    for (auto Code : Codes)
        if (Code > 0xFF)
            Narrow = false;
    if (Codes.empty())
    {
        Out.Form = 0;
        return Out;
    }
    Out.Form = Narrow ? 1 : 2;
    for (auto Code : Codes)
    {
        if (Narrow)
        {
            Out.Text += char(Code);
            continue;
        }
        auto Unit = [&](std::uint32_t U)
        {
            Out.Text += char(U & 0xFF);
            Out.Text += char(U >> 8);
        };
        if (Code >= 0x10000)
        {
            Code -= 0x10000;
            Unit(0xD800 + (Code >> 10));
            Unit(0xDC00 + (Code & 0x3FF));
        }
        else
            Unit(Code);
    }
    return Out;
}

// ---------------------------------------------------------------- a flat listing, for comparing decoders

inline std::string Hex(const std::string& Bytes)
{
    static const char Digits[] = "0123456789abcdef";
    std::string Out;
    for (unsigned char C : Bytes)
    {
        Out += Digits[C >> 4];
        Out += Digits[C & 15];
    }
    return Out;
}
inline std::string Bits32(std::uint32_t Word)
{
    static const char Digits[] = "0123456789abcdef";
    std::string Out = "f:";
    for (int I = 7; I >= 0; --I)
        Out += Digits[(Word >> (4 * I)) & 15];
    return Out;
}
// One line per leaf: path=value. Floats as f:<bits>, names as n:<UTF-8 hex>, booleans true/false, integers
// in decimal (I32 signed), arrays with a path#=count line, vectors as three items, rotators by member.
inline void Flatten(const Value& V, const std::string& Path, std::vector<std::string>& Out)
{
    switch (V.Is)
    {
        case Kind::Bool:
            Out.push_back(Path + "=" + (V.Set() ? "true" : "false"));
            return;
        case Kind::I32:
            Out.push_back(Path + "=" + std::to_string(std::int32_t(std::uint32_t(V.Bits))));
            return;
        case Kind::U32:
        case Kind::U8:
        case Kind::U16:
        case Kind::U64:
            Out.push_back(Path + "=" + std::to_string(V.Bits));
            return;
        case Kind::F32:
            Out.push_back(Path + "=" + Bits32(std::uint32_t(V.Bits)));
            return;
        case Kind::Name:
            Out.push_back(Path + "=n:" + Hex(NameUtf8(V)));
            return;
        case Kind::Vec:
        case Kind::Quat:
        {
            const int N = V.Is == Kind::Quat ? 4 : 3;
            Out.push_back(Path + "#=" + std::to_string(N));
            for (int I = 0; I < N; ++I)
                Out.push_back(Path + "[" + std::to_string(I) + "]=" + Bits32(V.Words[I]));
            return;
        }
        case Kind::Rot:
        {
            const char* Names[] = {"Pitch", "Yaw", "Roll"};
            for (int I = 0; I < 3; ++I)
                Out.push_back(Path + "." + Names[I] + "=" + Bits32(V.Words[I]));
            return;
        }
        case Kind::Array:
        case Kind::Repeat:
        case Kind::Bools:
            Out.push_back(Path + "#=" + std::to_string(V.Items.size()));
            for (std::size_t I = 0; I < V.Items.size(); ++I)
                Flatten(V.Items[I], Path + "[" + std::to_string(I) + "]", Out);
            return;
        case Kind::Struct:
        case Kind::SoftPath:
            for (const auto& M : V.Members)
                Flatten(M.second, Path.empty() ? M.first : Path + "." + M.first, Out);
            return;
    }
}

// ---------------------------------------------------------------- placement numbers

// The 64-bit FNV-1a hash of some text.
inline std::uint64_t Fnv(const std::string& Text, std::uint64_t Hash = UINT64_C(14695981039346656037))
{
    for (unsigned char C : Text)
        Hash = (Hash ^ C) * UINT64_C(1099511628211);
    return Hash;
}
// The placement numbers of a mod's list for one map: the high word is 0x6B and 24 bits of the mod's ID and the
// map's path (the game's own numbers have a high word below 0x41000000 or of all ones), the low word 32 bits of
// what is numbered and a salt. Never 0 and never the game's "none".
inline std::uint32_t ListWord(const std::string& Mod, const std::string& Map)
{
    std::string Lower = Map;
    for (char& C : Lower)
        if (C >= 'A' && C <= 'Z')
            C = char(C + 32);
    return UINT32_C(0x6B000000) | std::uint32_t(Fnv(Mod + "\n" + Lower) & 0xFFFFFF);
}
inline std::uint64_t Number(std::uint32_t List, const std::string& What, std::uint32_t Salt)
{
    std::uint32_t Low = std::uint32_t(Fnv(What + "\n" + std::to_string(Salt)) & 0xFFFFFFFFu);
    if (Low == 0 || Low == 0xFFFFFFFFu)
        Low = 1;
    return (std::uint64_t(List) << 32) | Low;
}
// Whether a number could be one of the game's own: Kadan's are never.
inline bool GameRange(std::uint64_t Id)
{
    return (Id >> 56) != 0x6B;
}

// Numbers for what a list names, in the order given: each the first salt that is not already taken, so that
// the same names give the same numbers on every export.
inline std::vector<std::uint64_t> Numbers(
    std::uint32_t List, const std::vector<std::string>& What, std::vector<std::uint64_t> Taken = {})
{
    std::vector<std::uint64_t> Out;
    for (const auto& One : What)
    {
        for (std::uint32_t Salt = 0;; ++Salt)
        {
            const std::uint64_t Id = Number(List, One, Salt);
            bool Used = false;
            for (auto T : Taken)
                if (T == Id)
                {
                    Used = true;
                    break;
                }
            if (!Used)
            {
                Out.push_back(Id);
                Taken.push_back(Id);
                break;
            }
        }
    }
    return Out;
}

}
} // namespace P4MapKit::Placement

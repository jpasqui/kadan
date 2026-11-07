// Stored UE property text and sequencer channels. The original property tag,
// not a JSON shape, selects the native codec. Unsupported values stay unread.
#pragma once
#include "blueprint_bytecode.h"
#include "package_reader.h"
#include <iomanip>

namespace P4MapKit { namespace Blueprint {
inline std::string PropertyNumber(double Value, bool Double = false)
{
    if (!std::isfinite(Value)) throw Error("a number that is not finite");
    std::ostringstream Out; Out.imbue(std::locale::classic());
    Out << std::setprecision(Double ? 17 : 9) << Value;
    auto Text = Out.str();
    if (Text.find('e') != std::string::npos)
    {
        std::ostringstream Fixed; Fixed.imbue(std::locale::classic());
        Fixed << std::fixed << std::setprecision(Double ? 340 : 60) << Value;
        Text = Fixed.str(); while (!Text.empty() && Text.back() == '0') Text.pop_back();
    }
    if (Text.find('.') == std::string::npos) Text += ".0";
    return Text;
}
inline std::string PropertyQuote(const std::string& Text)
{
    std::string Out = "\"";
    for (const auto C : Text)
    { if (C == '\\' || C == '"') { Out += '\\'; Out += C; } else if (C == '\n') Out += "\\n"; else if (C == '\r') Out += "\\r"; else Out += C; }
    return Out + '"';
}
class ValueReader
{
public:
    const std::vector<std::uint8_t>& Data;
    const std::vector<std::string>& Names;
    std::size_t At, End;
    ValueReader(const std::vector<std::uint8_t>& Bytes, const std::vector<std::string>& InNames, std::size_t Start, std::size_t Stop)
        : Data(Bytes), Names(InNames), At(Start), End(Stop)
    { if (At > End || End > Data.size()) throw Error("Invalid reader bounds"); }
    std::size_t Take(std::size_t Size)
    { if (Size > End - At) throw Error("Truncated payload at 0x" + HexNumber(At) + ". " + std::to_string(Size) + " more bytes are needed."); const auto Old = At; At += Size; return Old; }
    std::uint64_t U(std::size_t Width)
    { if (!Width || Width > 8) throw Error("invalid integer width"); const auto Start = Take(Width); std::uint64_t Out = 0; for (std::size_t N = 0; N < Width; ++N) Out |= std::uint64_t(Data[Start + N]) << (8 * N); return Out; }
    std::int64_t I(std::size_t Width)
    { const auto Bits = U(Width), Sign = UINT64_C(1) << (Width * 8 - 1); return Bits & Sign ? -1 - std::int64_t((Width == 8 ? UINT64_MAX : ((UINT64_C(1) << (Width * 8)) - 1)) - Bits) : std::int64_t(Bits); }
    double F(bool Double = false)
    { const auto Bits = U(Double ? 8 : 4); if (Double) { double V; std::memcpy(&V, &Bits, 8); return V; } const auto Small = std::uint32_t(Bits); float V; std::memcpy(&V, &Small, 4); return V; }
    std::size_t Count(std::size_t Maximum = 100000)
    { const auto Number = I(4); if (Number < 0 || std::uint64_t(Number) > Maximum) throw Error("Invalid count " + std::to_string(Number)); return std::size_t(Number); }
    std::string Name()
    { const auto Start = Take(8); At = Start; const auto Index = I(4), Number = I(4); if (Index < 0 || std::uint64_t(Index) >= Names.size() || Number < 0) throw Error("Invalid FName"); return Names[std::size_t(Index)] + (Number ? "_" + std::to_string(Number - 1) : ""); }
    std::string String()
    { const auto Start = At; FByteReader Reader(Data, At, End); std::string Out; if (!Reader.String(Out)) throw Error("FString " + Reader.Error.Code + " at 0x" + HexNumber(Start)); At = Reader.Tell(); return Out; }
    void Done() const
    { if (At != End) throw Error("Unexpected payload tail at 0x" + HexNumber(At) + ", end 0x" + HexNumber(End)); }
    FPropertyEnvelope Tag()
    {
        FPropertyEnvelope Out; Out.TagOffset = At; Out.Name = Name(); if (Out.Name == "None") return Out;
        Out.Type = Name(); const auto Span = Take(8); At = Span; const auto Size = I(4), Index = I(4); if (Size < 0 || Index < 0) throw Error("Negative property size/index"); Out.Size = std::size_t(Size); Out.ArrayIndex = std::size_t(Index);
        if (Out.Type == "StructProperty") { Out.Detail.push_back(Name()); Take(16); }
        else if (Out.Type == "BoolProperty") { const auto Bool = U(1); if (Bool > 1 || Size != 0) throw Error("Invalid boolean tag"); Out.BoolValue = bool(Bool); }
        else if (Out.Type == "ByteProperty" || Out.Type == "EnumProperty" || Out.Type == "ArrayProperty" || Out.Type == "SetProperty") Out.Detail.push_back(Name());
        else if (Out.Type == "MapProperty") { Out.Detail.push_back(Name()); Out.Detail.push_back(Name()); }
        else
        {
            static const std::set<std::string> Plain{"IntProperty","Int8Property","Int16Property","Int64Property","UInt16Property","UInt32Property","UInt64Property","FloatProperty","DoubleProperty","NameProperty","ObjectProperty","SoftObjectProperty","LazyObjectProperty","StrProperty","TextProperty","InterfaceProperty","DelegateProperty","MulticastDelegateProperty","MulticastSparseDelegateProperty","MulticastInlineDelegateProperty","FieldPathProperty"};
            if (!Plain.count(Out.Type)) throw Error("Unsupported property tag " + Out.Type);
        }
        const auto Guid = U(1); if (Guid > 1) throw Error("Invalid property GUID flag"); if (Guid) Take(16);
        Out.PayloadOffset = At; Take(Out.Size); return Out;
    }
};
inline J FloatChannel(ValueReader& Reader)
{
    const auto Before = Reader.U(1), After = Reader.U(1); const auto Size = Reader.I(4), Count = Reader.I(4);
    if (Size != 4 || Count < 0 || std::uint64_t(Count) > (Reader.End - Reader.At) / 4) throw Error("a float channel whose times do not read");
    std::vector<std::int64_t> Times; for (std::int64_t N = 0; N < Count; ++N) Times.push_back(Reader.I(4));
    const auto ValueSize = Reader.I(4), Values = Reader.I(4);
    if (ValueSize != 28 || Values != Count || std::uint64_t(Count) * 28 + 16 != Reader.End - Reader.At) throw Error("a float channel whose values do not read");
    J Keys = J::Array();
    for (const auto Frame : Times)
    {
        const auto Value = Reader.F(), Arrive = Reader.F(), Leave = Reader.F(), ArriveWeight = Reader.F(), LeaveWeight = Reader.F();
        const auto Weight = Reader.U(1); Reader.Take(3); const auto Interpolation = Reader.U(1), Tangent = Reader.U(1), Padding = Reader.U(1); Reader.Take(1);
        Keys.Items.push_back(J::Array({J(Frame), PropertyNumber(Value), J(Interpolation), J(Tangent), PropertyNumber(Arrive), PropertyNumber(Leave), J(Weight), PropertyNumber(ArriveWeight), PropertyNumber(LeaveWeight), J(Padding)}));
    }
    const auto Default = Reader.F(); const auto Has = Reader.I(4), Numerator = Reader.I(4), Denominator = Reader.I(4);
    if (Has != 0 && Has != 1) throw Error("a float channel whose default does not read");
    return J::Object({{"pre", J(Before)}, {"post", J(After)}, {"keys", Keys}, {"default", PropertyNumber(Default)}, {"has_default", bool(Has)}, {"tick", J::Array({J(Numerator), J(Denominator)})}});
}
class ValueText
{
    const FPackageInventory& Package;
    const std::vector<std::uint8_t>& Body;
    std::function<std::vector<std::string>(const std::vector<std::string>&)> Rebase;
    J Trail = J::Array(), Channels = J::Array();
    std::size_t Depth = 0, Nodes = 0, TextBytes = 0;
    std::vector<std::string> StructNames;
    int InMap = 0;
    bool PreserveSettingsMembers = false;
    struct Result { bool Channel = false; std::string Text; Result(const std::string& InText = "") : Text(InText) {} Result(const char* InText) : Text(InText) {} };
    std::string Charge(const std::string& Value)
    { if (Value.size() > 64 * 1024 * 1024 - TextBytes) throw Error("property text budget"); TextBytes += Value.size(); return Value; }
    static std::string Join(const std::vector<std::string>& Values, const std::string& Separator = ",")
    { std::string Out; for (std::size_t N = 0; N < Values.size(); ++N) { if (N) Out += Separator; Out += Values[N]; } return Out; }
    std::string Soft(ValueReader& Reader)
    { const auto Path = Reader.Name(), Sub = Reader.String(); return (Path == "None" ? "" : Path) + (Sub.empty() ? "" : ':' + Sub); }
    std::string Guid(ValueReader& Reader)
    { std::string Out; for (int N = 0; N < 4; ++N) { const auto Text = HexNumber(Reader.U(4), true); Out += std::string(8 - Text.size(), '0') + Text; } return Out; }
    struct Layout { const char* Type; std::vector<std::string> Members; };
    static const std::map<std::string, Layout>& Native()
    {
        static const std::map<std::string, Layout> Values{
            {"Vector", {"f", {"X","Y","Z"}}}, {"Rotator", {"f", {"Pitch","Yaw","Roll"}}}, {"Vector2D", {"f", {"X","Y"}}},
            {"Vector4", {"f", {"X","Y","Z","W"}}}, {"Quat", {"f", {"X","Y","Z","W"}}}, {"Plane", {"f", {"X","Y","Z","W"}}},
            {"LinearColor", {"f", {"R","G","B","A"}}}, {"Color", {"B", {"B","G","R","A"}}}, {"IntPoint", {"i", {"X","Y"}}},
            {"IntVector", {"i", {"X","Y","Z"}}}, {"FrameNumber", {"i", {"Value"}}}};
        return Values;
    }
    std::vector<std::string> Members(ValueReader& Reader)
    {
        std::vector<std::string> Out;
        for (std::size_t N = 0; N < 100000; ++N)
        {
            const auto Item = Reader.Tag(); if (Item.Name == "None") return Out;
            const auto Old = Trail.Items.size(); Trail.Items.push_back(Item.Name); if (Item.ArrayIndex) Trail.Items.push_back(J(std::uint64_t(Item.ArrayIndex)));
            const auto Held = Value(Item, true); Trail.Items.resize(Old);
            if (!Held.Channel) Out.push_back(Item.Name + (Item.ArrayIndex ? '[' + std::to_string(Item.ArrayIndex) + ']' : "") + '=' + Held.Text);
        }
        throw Error("property limit");
    }
    Result Structure(const std::string& Name, std::size_t Start, std::size_t End)
    {
        StructNames.push_back(Name); struct StructScope { std::vector<std::string>& Names; ~StructScope() { Names.pop_back(); } } StructGuard{StructNames};
        ValueReader Reader(Body, Package.Names, Start, End); const auto NativeFound = Native().find(Name);
        if (NativeFound != Native().end())
        {
            const auto& Spec = NativeFound->second; const auto Size = Spec.Members.size() * (Spec.Type[0] == 'B' ? 1 : 4);
            if (End - Start != Size) throw Error(Name + " of " + std::to_string(End - Start) + " bytes"); std::vector<std::string> Parts;
            for (const auto& Member : Spec.Members) Parts.push_back(Member + '=' + (Spec.Type[0] == 'f' ? PropertyNumber(Reader.F()) : Spec.Type[0] == 'B' ? std::to_string(Reader.U(1)) : std::to_string(Reader.I(4))));
            return '(' + Join(Parts) + ')';
        }
        if (Name == "SoftObjectPath" || Name == "SoftClassPath") { const auto Path = Soft(Reader); Reader.Done(); return PropertyQuote(Path); }
        if (Name == "Guid") { const auto Out = Guid(Reader); Reader.Done(); return Out; }
        if (Name == "GameplayTag") { const auto Out = "(TagName=" + PropertyQuote(Reader.Name()) + ')'; Reader.Done(); return Out; }
        if (Name == "GameplayTagContainer")
        { std::vector<std::string> Parts; const auto Count = Reader.Count(); for (std::size_t N = 0; N < Count; ++N) Parts.push_back("(TagName=" + PropertyQuote(Reader.Name()) + ')'); Reader.Done(); return "(GameplayTags=(" + Join(Parts) + "))"; }
        if (Name == "Box" || Name == "Box2D")
        { const auto Width = Name == "Box" ? 3 : 2; std::vector<std::string> Min, Max; for (int N = 0; N < Width * 2; ++N) (N < Width ? Min : Max).push_back(std::string(1, "XYZ"[N % Width]) + '=' + PropertyNumber(Reader.F())); const auto Valid = Reader.U(1); Reader.Done(); return "(Min=(" + Join(Min) + "),Max=(" + Join(Max) + "),IsValid=" + std::to_string(Valid) + ')'; }
        if (Name == "NavAgentSelector")
        { const auto Bits = Reader.U(4); if (Bits >> 16 != 0x7fff) throw Error("NavAgentSelector 0x" + HexNumber(Bits)); Reader.Done(); std::vector<std::string> Parts; for (int N = 0; N < 16; ++N) Parts.push_back("bSupportsAgent" + std::to_string(N) + '=' + (Bits >> N & 1 ? "True" : "False")); return '(' + Join(Parts) + ')'; }
        if (Name == "MovieSceneEventPtrs") return "()";
        if (Name == "MovieSceneFrameRange")
        { static const char* Kinds[] = {"Exclusive", "Inclusive", "Open"}; std::vector<std::string> Parts; for (int N = 0; N < 2; ++N) { const auto Kind = Reader.U(1); const auto Frame = Reader.I(4); if (Kind > 2) throw Error("Invalid frame range bound"); Parts.push_back(std::string(Kinds[Kind]) + ':' + std::to_string(Kind == 2 ? 0 : Frame)); } Reader.Done(); return Join(Parts, ":"); }
        if (Name == "MovieSceneFloatChannel")
        { if (InMap) throw Error("a key channel inside a map"); auto Channel = FloatChannel(Reader); Channel["at"] = Trail; Channels.Items.push_back(Channel); Result Out; Out.Channel = true; return Out; }
        if (Name == "PerPlatformFloat" || Name == "PerPlatformInt" || Name == "PerPlatformBool")
        { if (!Reader.U(4)) throw Error(Name + " that is not cooked"); const auto Out = Name == "PerPlatformFloat" ? PropertyNumber(Reader.F()) : Name == "PerPlatformInt" ? std::to_string(Reader.I(4)) : Reader.U(4) ? "True" : "False"; Reader.Done(); return "(Default=" + Out + ')'; }
        std::vector<std::string> Parts; try { Parts = Members(Reader); } catch (const Error& Failure) { throw Error("struct " + Name + ": " + Failure.what()); }
        if (Reader.At != End) throw Error("struct " + Name + ": " + std::to_string(End - Reader.At) + " bytes after its properties"); return '(' + Join(Parts) + ')';
    }
    std::string Text(ValueReader& Reader)
    {
        Reader.U(4); const auto Kind = Reader.I(1);
        if (Kind == -1) return Reader.U(4) ? "INVTEXT(" + PropertyQuote(Reader.String()) + ')' : "\"\"";
        if (Kind == 0) { const auto Space = Reader.String(), Key = Reader.String(), Source = Reader.String(); return "NSLOCTEXT(" + PropertyQuote(Space) + ", " + PropertyQuote(Key) + ", " + PropertyQuote(Source) + ')'; }
        if (Kind == 11) { const auto Table = Reader.Name(); return "LOCTABLE(" + PropertyQuote(Table) + ", " + PropertyQuote(Reader.String()) + ')'; }
        throw Error("text history kind " + std::to_string(Kind));
    }
    std::string Element(const std::string& Kind, ValueReader& Reader)
    {
        static const std::map<std::string, int> Integer{{"IntProperty",4},{"Int64Property",8},{"UInt32Property",-4},{"UInt64Property",-8},{"Int8Property",1},{"Int16Property",2},{"UInt16Property",-2}};
        const auto Found = Integer.find(Kind); if (Found != Integer.end()) return Found->second > 0 ? std::to_string(Reader.I(std::size_t(Found->second))) : std::to_string(Reader.U(std::size_t(-Found->second)));
        if (Kind == "FloatProperty" || Kind == "DoubleProperty") return PropertyNumber(Reader.F(Kind == "DoubleProperty"), Kind == "DoubleProperty");
        if (Kind == "BoolProperty") return Reader.U(1) ? "True" : "False";
        if (Kind == "ByteProperty") return std::to_string(Reader.U(1));
        if (Kind == "NameProperty" || Kind == "EnumProperty") return PropertyQuote(Reader.Name());
        if (Kind == "StrProperty") return PropertyQuote(Reader.String());
        if (Kind == "TextProperty") return Text(Reader);
        if (Kind == "ObjectProperty" || Kind == "InterfaceProperty") return PropertyQuote(ObjectPath(std::int32_t(Reader.I(4))));
        if (Kind == "SoftObjectProperty") return PropertyQuote(Soft(Reader));
        throw Error("element of " + Kind);
    }
    Result Value(const FPropertyEnvelope& Item, bool Nested = false)
    {
        if (++Nodes > 1000000 || Depth >= 32) throw Error("property text nesting budget");
        struct Scope { std::size_t& Level; Scope(std::size_t& InLevel) : Level(InLevel) { ++Level; } ~Scope() { --Level; } } Guard(Depth);
        ValueReader Reader(Body, Package.Names, Item.PayloadOffset, Item.PayloadOffset + Item.Size); const auto& Kind = Item.Type;
        if (Kind == "BoolProperty") return Item.BoolValue ? "True" : "False";
        if (Kind == "StructProperty") return Structure(Item.Detail.at(0), Reader.At, Reader.End);
        if (Kind == "ArrayProperty" || Kind == "SetProperty")
        {
            if (Kind == "SetProperty" && Reader.Count()) throw Error("a set with removed elements"); const auto Count = Reader.Count(1000000); std::vector<std::string> Parts;
            const auto& Inner = Item.Detail.at(0);
            if (Inner == "StructProperty")
            {
                const auto Tag = Reader.Tag(); if (Tag.Name == "None" || Tag.Type != "StructProperty") throw Error("array of structs without its inner tag");
                const auto& Name = Tag.Detail.at(0); const auto Found = Native().find(Name); std::size_t Stride = 0;
                if (Found != Native().end()) { Stride = Found->second.Members.size() * (Found->second.Type[0] == 'B' ? 1 : 4); if (Stride * Count != Tag.Size) throw Error("array of " + Name + ": size"); }
                else if (Name == "Guid") { Stride = 16; if (Stride * Count != Tag.Size) throw Error("array of Guid: size"); }
                if (Stride) for (std::size_t N = 0; N < Count; ++N) Parts.push_back(Structure(Name, Tag.PayloadOffset + N * Stride, Tag.PayloadOffset + (N + 1) * Stride).Text);
                else
                {
                    ValueReader InnerReader(Body, Package.Names, Tag.PayloadOffset, Tag.PayloadOffset + Tag.Size);
                    for (std::size_t Place = 0; Place < Count; ++Place)
                    {
                        if (Name == "SoftObjectPath" || Name == "SoftClassPath") Parts.push_back(PropertyQuote(Soft(InnerReader)));
                        else if (Name == "GameplayTag") Parts.push_back("(TagName=" + PropertyQuote(InnerReader.Name()) + ')');
                        else { const auto Old = Trail.Items.size(); Trail.Items.push_back(J(std::uint64_t(Place))); try { Parts.push_back('(' + Join(Members(InnerReader)) + ')'); } catch (const Error& Failure) { Trail.Items.resize(Old); throw Error("array of struct " + Name + ": " + Failure.what()); } Trail.Items.resize(Old); }
                    }
                    if (InnerReader.At != InnerReader.End) throw Error("array of struct " + Name + ": " + std::to_string(InnerReader.End - InnerReader.At) + " bytes left");
                }
                Reader.Done();
            }
            else { for (std::size_t N = 0; N < Count; ++N) Parts.push_back(Element(Inner, Reader)); Reader.Done(); }
            return Parts.empty() ? "" : '(' + Join(Parts) + ')';
        }
        if (Kind == "MapProperty")
        {
            if (Reader.Count()) throw Error("a map with removed keys"); const auto Count = Reader.Count(1000000); std::vector<std::string> Parts; ++InMap;
            struct Scope { int& Counter; ~Scope() { --Counter; } } GuardMap{InMap};
            for (std::size_t N = 0; N < Count; ++N)
            {
                std::vector<std::string> Pair;
                for (std::size_t Side = 0; Side < Item.Detail.size(); ++Side)
                {
                    const auto& Inner = Item.Detail[Side];
                    // Captured field schema: FLevelSequenceBindingReferences::
                    // BindingIdToReferences is Guid -> tagged BindingReferenceArray.
                    // A malformed tagged key elsewhere is never guessed to be Guid.
                    if (Inner == "StructProperty" && Side == 0 && Item.Name == "BindingIdToReferences" && !StructNames.empty() && StructNames.back() == "LevelSequenceBindingReferences") Pair.push_back(Guid(Reader));
                    else if (Inner == "StructProperty")
                    { try { Pair.push_back('(' + Join(Members(Reader)) + ')'); } catch (const Error& Failure) { throw Error(Pair.empty() ? Failure.what() : std::string("a map of structs: ") + Failure.what()); } }
                    else Pair.push_back(Element(Inner, Reader));
                }
                Parts.push_back('(' + Join(Pair) + ')');
            }
            Reader.Done(); return Parts.empty() ? "" : '(' + Join(Parts) + ')';
        }
        std::string Out;
        if (Kind == "ByteProperty") Out = Item.Size == 1 ? std::to_string(Reader.U(1)) : Reader.Name();
        else if (Kind == "NameProperty" || Kind == "EnumProperty") { Out = Reader.Name(); if (Nested) Out = PropertyQuote(Out); }
        else if (Kind == "StrProperty") { Out = Reader.String(); if (Nested) Out = PropertyQuote(Out); }
        else if (Kind == "ObjectProperty" || Kind == "InterfaceProperty") { Out = ObjectPath(std::int32_t(Reader.I(4))); if (Nested && Out != "None") Out = PropertyQuote(Out); }
        else if (Kind == "SoftObjectProperty") { Out = Soft(Reader); if (Nested) Out = PropertyQuote(Out); }
        else if (Kind == "TextProperty") Out = Text(Reader);
        else
        {
            static const std::set<std::string> Scalars{"IntProperty","Int64Property","UInt32Property","UInt64Property","Int8Property","Int16Property","UInt16Property","FloatProperty","DoubleProperty"};
            if (!Scalars.count(Kind)) throw Error(Kind); Out = Element(Kind, Reader);
        }
        Reader.Done(); return Charge(Out);
    }
public:
    ValueText(const FPackageInventory& InPackage, const std::vector<std::uint8_t>& InBody,
        std::function<std::vector<std::string>(const std::vector<std::string>&)> InRebase = {}, bool InPreserveSettingsMembers = false)
        : Package(InPackage), Body(InBody), Rebase(InRebase), PreserveSettingsMembers(InPreserveSettingsMembers) {}
    std::string ObjectPath(std::int32_t Index) const
    {
        if (!Index) return "None"; std::string Path; if (!Package.Reference(Index, Path)) throw Error("Invalid object reference");
        if (Path.find("/Engine/UnknownPackage") == 0) throw Error("names an object the game does not ship"); if (Index < 0) return Path;
        std::vector<std::string> Parts; std::size_t Begin = 0;
        for (;;) { const auto Dot = Path.find('.', Begin); Parts.push_back(Path.substr(Begin, Dot == std::string::npos ? Dot : Dot - Begin)); if (Dot == std::string::npos) break; Begin = Dot + 1; }
        if (Rebase) Parts = Rebase(Parts); if (Parts.empty()) throw Error("empty rebased object path");
        std::string Out = Package.Package + '.' + Parts[0]; if (Parts.size() > 1) { Out += ':'; for (std::size_t N = 1; N < Parts.size(); ++N) { if (N > 1) Out += '.'; Out += Parts[N]; } } return Out;
    }
    // Whether a delegate value binds only the object that holds it. The class's own code binds such a
    // delegate on every object made from the template, so it is not a value the editor is given.
    bool BoundToItself(const FPropertyEnvelope& Item, std::int32_t Self) const
    {
        const bool Many = Item.Type == "MulticastDelegateProperty" || Item.Type == "MulticastInlineDelegateProperty" || Item.Type == "MulticastSparseDelegateProperty";
        if (!Self || (!Many && Item.Type != "DelegateProperty")) return false;
        try
        {
            ValueReader Reader(Body, Package.Names, Item.PayloadOffset, Item.PayloadOffset + Item.Size);
            const std::size_t Count = Many ? Reader.Count(65536) : 1;
            if (!Count) return false;
            for (std::size_t N = 0; N < Count; ++N) { if (std::int32_t(Reader.I(4)) != Self) return false; Reader.Name(); }
            Reader.Done(); return true;
        }
        catch (const Error&) { return false; }
    }
    // Retain original typed member envelopes before a struct is flattened to text.
    // Only logical Settings uses this contract; native admission still requires the
    // exact recovered component/struct identity in the editor, never this name alone.
    J SettingsMembers(const FPropertyEnvelope& Item)
    {
        const auto Inventory = ReadPropertyEnvelopes(Body, Package.Names, Item.PayloadOffset, Item.PayloadOffset + Item.Size);
        if (!Inventory.Error.Code.empty() || Inventory.End != Item.PayloadOffset + Item.Size)
            throw Error("Settings member envelopes: " + (Inventory.Error.Code.empty() ? std::string("unexpected-tail") : Inventory.Error.Code));
        J Members = J::Array();
        for (const auto& Member : Inventory.Properties)
        {
            Trail = J::Array(); Channels = J::Array(); InMap = 0;
            const auto Text = Value(Member);
            J Detail = J::Array(), StructGuid = J::Array(), PropertyGuid = J::Array();
            for (const auto& Name : Member.Detail) Detail.Items.push_back(Name);
            for (const auto Word : Member.StructGuid) StructGuid.Items.push_back(J(std::uint64_t(Word)));
            for (const auto Word : Member.PropertyGuid) PropertyGuid.Items.push_back(J(std::uint64_t(Word)));
            J Record = J::Object({{"name",Member.Name},{"index",J(std::uint64_t(Member.ArrayIndex))},{"type",Member.Type},
                {"detail",Detail},{"tag_offset",J(std::uint64_t(Member.TagOffset))},{"payload_offset",J(std::uint64_t(Member.PayloadOffset))},
                {"payload_size",J(std::uint64_t(Member.Size))},{"struct_guid",StructGuid},{"has_guid",Member.HasGuid},{"property_guid",PropertyGuid}});
            if (!Text.Channel) Record["text"] = Charge(Text.Text);
            if (Member.Type == "StructProperty") Record["struct"] = Member.Detail.at(0);
            if (!Channels.Items.empty()) Record["channels"] = Channels;
            Members.Items.push_back(std::move(Record));
        }
        return Members;
    }
    J Values(const FPackageExport& Export)
    {
        ValueReader Reader(Body, Package.Names, Export.Offset, Export.Offset + Export.Size); J Held = J::Array(), Unread = J::Array();
        const bool Listed = !Package.Exports.empty() && &Export >= Package.Exports.data() && &Export < Package.Exports.data() + Package.Exports.size();
        const std::int32_t Self = Listed ? std::int32_t(&Export - Package.Exports.data()) + 1 : 0;
        for (std::size_t N = 0; N < 100000; ++N)
        {
            const auto Item = Reader.Tag(); if (Item.Name == "None") return J::Object({{"held", Held}, {"unread", Unread}});
            if (BoundToItself(Item, Self)) continue;
            Trail = J::Array(); Channels = J::Array(); InMap = 0;
            try
            {
                const bool TypedSettings = PreserveSettingsMembers && Item.Name == "Settings" && Item.Type == "StructProperty" && Item.Detail.at(0) == "PostProcessSettings";
                const auto Members = TypedSettings ? SettingsMembers(Item) : J::Array();
                Trail = J::Array(); Channels = J::Array(); InMap = 0;
                const auto Text = Value(Item);
                J Record = J::Object({{"name", Item.Name}, {"index", J(std::uint64_t(Item.ArrayIndex))}, {"type", Item.Type}});
                if (!Text.Channel) Record["text"] = Charge(Text.Text);
                if (Item.Type == "StructProperty") Record["struct"] = Item.Detail.at(0);
                if (!Channels.Items.empty()) Record["channels"] = Channels;
                if (TypedSettings) Record["members"] = Members;
                Held.Items.push_back(Record);
            }
            catch (const Error& Failure) { Unread.Items.push_back(J::Object({{"name", Item.Name}, {"type", Item.Type}, {"why", Failure.what()}})); }
        }
        throw Error("property limit");
    }
};
}} // namespace P4MapKit::Blueprint

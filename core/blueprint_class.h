// Native UStruct/UClass/UFunction boundary for converted UE4.26 packages.
// Stored-value decoding is supplied separately; opaque objects cannot qualify
// an import merely because the class and its script have been inventoried.
#pragma once
#include "blueprint_bytecode.h"
#include "property_decoder.h"

namespace P4MapKit { namespace Blueprint {
class ClassReader
{
    const FPackageInventory& Package;
    const std::vector<std::uint8_t>& Body;
    std::size_t Properties = 0;
    void Check(bool Good, const FByteReader& Reader) const
    { if (!Good) throw Error(Reader.Error.Code + " at byte " + std::to_string(Reader.Error.Offset)); }
    std::string Name(FByteReader& Reader) const
    { std::string Out; Check(Reader.Name(Package.Names, Out), Reader); return Out; }
    std::uint64_t Unsigned(FByteReader& Reader, std::size_t Width) const
    { std::uint64_t Out = 0; Check(Reader.Unsigned(Width, Out), Reader); return Out; }
    std::int32_t Integer(FByteReader& Reader) const
    { std::int32_t Out = 0; Check(Reader.I32(Out), Reader); return Out; }
    std::size_t Count(FByteReader& Reader, std::size_t Max = 100000) const
    { std::size_t Out = 0; Check(Reader.Count(Out, Max), Reader); return Out; }
    J Reference(FByteReader& Reader) const { return Reference(Integer(Reader)); }
    J Reference(std::int32_t Index) const
    { std::string Path; if (!Package.Reference(Index, Path)) throw Error("invalid-native-class-reference"); return Index ? J(Path) : J(); }
    J Property(FByteReader& Reader, const std::string& Kind, std::size_t Depth)
    {
        if (Depth >= 24 || ++Properties > 1000000) throw Error("native-property-record-budget");
        const auto PropertyName = Name(Reader); Check(Reader.Skip(4), Reader);
        const auto Dim = Integer(Reader), Size = Integer(Reader); const auto Flags = Unsigned(Reader, 8);
        if (Dim < 1 || Dim > 1000000 || Size < 0) throw Error("native-property-layout");
        Check(Reader.Skip(2), Reader); const auto Notify = Name(Reader); Check(Reader.Skip(1), Reader);
        J Out = J::Object({{"name", PropertyName}, {"type", Kind}, {"array_dim", J(int(Dim))},
            {"element_size", J(int(Size))}, {"flags", "0x" + HexNumber(Flags)}});
        if (Notify != "None") Out["rep_notify"] = Notify;
        if (Kind == "ObjectProperty" || Kind == "WeakObjectProperty" || Kind == "LazyObjectProperty" || Kind == "SoftObjectProperty") Out["class"] = Reference(Reader);
        else if (Kind == "ClassProperty" || Kind == "SoftClassProperty") { Out["class"] = Reference(Reader); Out["meta_class"] = Reference(Reader); }
        else if (Kind == "InterfaceProperty") Out["interface"] = Reference(Reader);
        else if (Kind == "StructProperty") Out["struct"] = Reference(Reader);
        else if (Kind == "ByteProperty") Out["enum"] = Reference(Reader);
        else if (Kind == "EnumProperty") { Out["enum"] = Reference(Reader); const auto Inner = Name(Reader); Out["underlying"] = Property(Reader, Inner, Depth + 1); }
        else if (Kind == "BoolProperty")
        { Check(Reader.Skip(5), Reader); const auto Native = Unsigned(Reader, 1); if (Native > 1) throw Error("native-property-bool"); Out["native_bool"] = J(bool(Native)); }
        else if (Kind == "ArrayProperty" || Kind == "SetProperty") { const auto Inner = Name(Reader); Out["inner"] = Property(Reader, Inner, Depth + 1); }
        else if (Kind == "MapProperty")
        { const auto Key = Name(Reader); Out["key"] = Property(Reader, Key, Depth + 1); const auto Value = Name(Reader); Out["value"] = Property(Reader, Value, Depth + 1); }
        else if (Kind == "DelegateProperty" || Kind == "MulticastDelegateProperty" || Kind == "MulticastInlineDelegateProperty" || Kind == "MulticastSparseDelegateProperty") Out["signature"] = Reference(Reader);
        else if (Kind == "FieldPathProperty") Out["field_class"] = Name(Reader);
        else
        {
            const std::set<std::string> Plain{"IntProperty","Int8Property","Int16Property","Int64Property","UInt16Property","UInt32Property","UInt64Property","FloatProperty","DoubleProperty","NameProperty","StrProperty","TextProperty"};
            if (!Plain.count(Kind)) throw Error("Unknown property record " + Kind);
        }
        return Out;
    }
    struct Head { J Record; std::vector<std::uint8_t> Script; };
    Head StructHead(FByteReader& Reader)
    {
        const auto Tags = ReadPropertyEnvelopes(Body, Package.Names, Reader.Tell(), Reader.Limit());
        if (!Tags.Error.Code.empty()) throw Error(Tags.Error.Code + " at byte " + std::to_string(Tags.Error.Offset));
        Check(Reader.Skip(Tags.End - Reader.Tell()), Reader);
        if (Integer(Reader) != 0) throw Error("native-object-guid-requires-adapter");
        Head Out; Out.Record = J::Object({{"super", Reference(Reader)}, {"children", J::Array()}, {"properties", J::Array()}});
        auto Number = Count(Reader); for (std::size_t Index = 0; Index < Number; ++Index) Out.Record["children"].Items.push_back(Reference(Reader));
        Number = Count(Reader); for (std::size_t Index = 0; Index < Number; ++Index) { const auto Kind = Name(Reader); Out.Record["properties"].Items.push_back(Property(Reader, Kind, 0)); }
        const auto Runtime = Integer(Reader), Storage = Integer(Reader);
        if (Runtime < 0 || Storage < 0 || Storage > 1024 * 1024) throw Error("native-script-size-bound");
        Out.Record["bytecode_size"] = J(int(Runtime)); const auto At = Reader.Tell(); Check(Reader.Skip(std::size_t(Storage)), Reader);
        Out.Script.assign(Body.begin() + At, Body.begin() + Reader.Tell()); return Out;
    }
    J Function(const FPackageExport& Export)
    {
        FByteReader Reader(Body, Export.Offset, Export.Offset + Export.Size); auto Head = StructHead(Reader); auto& Out = Head.Record;
        const auto Flags = Unsigned(Reader, 4); Out["flags"] = J(Flags);
        if (Flags & 0x40)
        { const auto Value = Unsigned(Reader, 2); Out["rep_offset"] = J(Value < 32768 ? int(Value) : int(Value) - 65536); }
        Out["event_graph_function"] = Reference(Reader); Out["event_graph_call_offset"] = J(int(Integer(Reader)));
        if (Reader.Tell() != Reader.Limit()) throw Error("bytes-after-native-function");
        Out["script_size"] = J(std::uint64_t(Head.Script.size()));
        Out["script"] = Head.Script.empty() ? J() : DecodeBytecode(Head.Script, Package, Out["bytecode_size"].I());
        return Out;
    }
    J GeneratedClass(const FPackageExport& Export)
    {
        FByteReader Reader(Body, Export.Offset, Export.Offset + Export.Size); auto Head = StructHead(Reader); auto& Out = Head.Record;
        if (!Head.Script.empty()) throw Error("class-holds-bytecode");
        Out["function_map"] = J::Object(); const auto Number = Count(Reader);
        for (std::size_t Index = 0; Index < Number; ++Index)
        { const auto Key = Name(Reader); if (Out["function_map"].Has(Key)) throw Error("duplicate-native-function-name"); Out["function_map"][Key] = Reference(Reader); }
        Out["class_flags"] = J(Unsigned(Reader, 4)); Out["class_within"] = Reference(Reader);
        Out["config_name"] = Name(Reader); Out["generated_by"] = Reference(Reader); Out["interfaces"] = J::Array();
        const auto Interfaces = Count(Reader);
        for (std::size_t Index = 0; Index < Interfaces; ++Index)
        { const auto Class = Reference(Reader); const auto Offset = Integer(Reader); const auto Implemented = Unsigned(Reader, 4);
            if (Implemented > 1) throw Error("noncanonical-interface-bool");
            Out["interfaces"].Items.push_back(J::Object({{"cls", Class}, {"pointer_offset", J(int(Offset))}, {"blueprint_implemented", bool(Implemented)}})); }
        Unsigned(Reader, 4); Name(Reader); const auto Cooked = Unsigned(Reader, 4);
        if (Cooked > 1) throw Error("noncanonical-class-cooked-bool"); Out["cooked"] = J(bool(Cooked)); Out["class_default_object"] = Reference(Reader);
        if (Reader.Tell() != Reader.Limit()) throw Error("bytes-after-native-class"); return Out;
    }
public:
    ClassReader(const FPackageInventory& InPackage, const std::vector<std::uint8_t>& InBody) : Package(InPackage), Body(InBody)
    { if (!Package.Error.Code.empty()) throw Error(Package.Error.Code + " at byte " + std::to_string(Package.Error.Offset)); }
    // This stage deliberately projects the exact bpread structure/native script
    // records. Deep property values and arbitrary object tails are another stage.
    J Structure()
    {
#ifdef P4_BLUEPRINT_CLASS_READ_CONTROL
        // Host-only instrumentation: count actual reader entries, including any
        // accidental second read through a generic helper. No production state.
        P4_BLUEPRINT_CLASS_READ_CONTROL();
#endif
        J Out = J::Object({{"package", Package.Package}, {"kind", J()}, {"class", J()}, {"functions", J::Object()}, {"objects", J::Array()}, {"problems", J::Array()}});
        for (std::size_t Index = 0; Index < Package.Exports.size(); ++Index)
        {
            const auto& Export = Package.Exports[Index]; std::string Path;
            if (!Package.Reference(Export.Class, Path)) throw Error("invalid-export-class"); if (!Path.empty() && Path[0] != '/') Path = Package.Package + '.' + Path;
            const auto Dot = Path.rfind('.'); const auto Leaf = Path.substr(Dot == std::string::npos ? 0 : Dot + 1);
            J Record = J::Object({{"index", J(std::uint64_t(Index + 1))}, {"name", Export.Name}, {"cls", Path}, {"outer", Reference(Export.Outer)}, {"template", Reference(Export.Template)}, {"flags", J(std::uint64_t(Export.Flags))}});
            if (Leaf == "BlueprintGeneratedClass" || Leaf == "AnimBlueprintGeneratedClass" || Leaf == "WidgetBlueprintGeneratedClass")
            {
                if (Out["class"].Kind != J::K::Null) throw Error("second-generated-class"); const auto Class = GeneratedClass(Export);
                for (const auto& Key : Class.Keys()) Record[Key] = Class[Key]; Out["class"] = Record;
                Out["kind"] = Leaf == "BlueprintGeneratedClass" ? "Blueprint" : Leaf == "AnimBlueprintGeneratedClass" ? "AnimBlueprint" : "WidgetBlueprint";
            }
            else if (Leaf == "Function")
            { const auto Func = Function(Export); for (const auto& Key : Func.Keys()) Record[Key] = Func[Key];
                if (Out["functions"].Has(Export.Name)) throw Error("duplicate-function-export"); Out["functions"][Export.Name] = Record; }
            else Out["objects"].Items.push_back(Record);
        }
        return Out;
    }
};
inline J ReadClassStructure(const FPackageInventory& Package, const std::vector<std::uint8_t>& Body)
{ return ClassReader(Package, Body).Structure(); }
}} // namespace P4MapKit::Blueprint

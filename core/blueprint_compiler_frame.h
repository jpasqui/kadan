// Exact source qualification for an empty transient Blueprint compiler frame.
// Other defaults and executable code remain the caller's independent comparison inputs.
#pragma once
#include "blueprint_class.h"
namespace P4MapKit { namespace Blueprint {
inline bool IsProvenEmptyCompilerFrame(const J& Doc, const FPackageInventory& P,
    const std::vector<std::uint8_t>& Body, std::int32_t DefaultIndex)
{
    try
    {
        if (DefaultIndex < 1 || std::size_t(DefaultIndex) > P.Exports.size()) return false;
        const auto& Default = P.Exports[std::size_t(DefaultIndex - 1)];
        if (!(Default.Flags & 0x10) || Default.Outer || Default.Class < 1 || std::size_t(Default.Class) > P.Exports.size()) return false;
        const auto& Class = Doc["class"];
        const auto& ClassExport = P.Exports[std::size_t(Default.Class - 1)];
        std::string DefaultPath, ClassKind;
        if (!P.Reference(DefaultIndex, DefaultPath) || !P.Reference(ClassExport.Class, ClassKind) ||
            ClassKind != "/Script/Engine.BlueprintGeneratedClass" || ClassExport.Outer ||
            Class["index"].I() != Default.Class || Class["class_default_object"].S() != DefaultPath || !Class["cooked"].B()) return false;
        const auto ClassName = Class["name"].S();
        if (ClassName.size() < 3 || ClassName.substr(ClassName.size()-2) != "_C") return false;
        const auto GraphName = "ExecuteUbergraph_" + ClassName.substr(0,ClassName.size()-2);
        const auto GraphPath = ClassName + '.' + GraphName;
        const J Expected = J::Object({{"name","UberGraphFrame"},{"type","StructProperty"},{"array_dim",1},
            {"element_size",16},{"flags","0x202000"},{"struct","/Script/Engine.PointerToUberGraphFrame"}});
        std::size_t Schemas = 0;
        for (const auto& Field : Class["properties"].Items) if (Field["name"].S() == "UberGraphFrame")
        { if (Field.Dump() != Expected.Dump()) return false; ++Schemas; }
        if (Schemas != 1 || Class["function_map"][GraphName].S() != GraphPath) return false;
        const auto ClassTags = ReadPropertyEnvelopes(Body,P.Names,ClassExport.Offset,ClassExport.Offset+ClassExport.Size);
        if (!ClassTags.Error.Code.empty()) return false;
        std::size_t Links = 0;
        for (const auto& Tag : ClassTags.Properties) if (Tag.Name == "UberGraphFunction")
        {
            if (Tag.Type != "ObjectProperty" || Tag.Size != 4 || Tag.ArrayIndex || Tag.HasGuid) return false;
            FByteReader R(Body,Tag.PayloadOffset,Tag.PayloadOffset+Tag.Size); std::int32_t Index = 0; std::string Path;
            if (!R.I32(Index) || Index < 1 || std::size_t(Index) > P.Exports.size() || !P.Reference(Index,Path) || Path != GraphPath) return false;
            const auto& Function = P.Exports[std::size_t(Index-1)];
            std::string Kind;
            if (Function.Outer != Default.Class || !P.Reference(Function.Class,Kind) || Kind != "/Script/CoreUObject.Function" ||
                Doc["functions"][GraphName]["index"].I() != Index || Doc["functions"][GraphName]["outer"].S() != ClassName) return false;
            ++Links;
        }
        if (Links != 1) return false;
        const auto Tags = ReadPropertyEnvelopes(Body,P.Names,Default.Offset,Default.Offset+Default.Size);
        if (!Tags.Error.Code.empty()) return false;
        std::size_t Frames = 0;
        for (const auto& Tag : Tags.Properties) if (Tag.Name == "UberGraphFrame")
        {
            if (Tag.Type != "StructProperty" || Tag.Detail != std::vector<std::string>{"PointerToUberGraphFrame"} ||
                Tag.Size != 8 || Tag.ArrayIndex || Tag.HasGuid || Tag.StructGuid != std::array<std::uint32_t,4>{}) return false;
            FByteReader R(Body,Tag.PayloadOffset,Tag.PayloadOffset+Tag.Size); std::string End;
            if (!R.Name(P.Names,End) || End != "None" || R.Tell() != R.Limit()) return false;
            ++Frames;
        }
        return Frames == 1;
    }
    catch (const std::exception&) { return false; }
}
inline bool IsProvenEmptyCompilerFrame(const FPackageInventory& P, const std::vector<std::uint8_t>& Body, std::int32_t DefaultIndex)
{
    try { return IsProvenEmptyCompilerFrame(ReadClassStructure(P,Body),P,Body,DefaultIndex); }
    catch (const std::exception&) { return false; }
}
}} // namespace P4MapKit::Blueprint

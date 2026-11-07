#pragma once
#include "P4NativeBlueprintReflection.h"
#include "P4MapKitCore/blueprint_graph_signatures.h"

// Typed runtime declarations project directly into the planner's value model.
// No reflection JSON is parsed and no absent declaration is synthesized.
inline P4MapKit::Blueprint::J P4BlueprintReflectionPlanValue(const FP4NativeBlueprintReflection& Snapshot)
{
    using P4MapKit::Blueprint::J;
    auto String = [](const FString& Text) { return J(std::string(TCHAR_TO_UTF8(*Text))); };
    auto Detail = [&](const FP4NativeReflectionProperty& Property)
    { return J::Object({{"enum_path", Property.EnumPath.IsEmpty() ? J() : String(Property.EnumPath)}}); };
    J Functions = J::Array(), Structures = J::Array(), Enums = J::Array();
    for (const FP4NativeReflectionFunction& Function : Snapshot.Functions)
    {
        J Parameters = J::Array();
        for (const FP4NativeReflectionProperty& Property : Function.Parameters)
            Parameters.Items.push_back(J::Object({{"name", String(Property.Name)}, {"is_return", J(false)},
                {"is_out", J(Property.IsOut)}, {"is_reference", J(Property.IsReference)}}));
        J Return;
        if (Function.Return.IsSet())
            Return = J::Object(
                {{"name", String(Function.Return->Name)}, {"type_detail", Detail(Function.Return.GetValue())}});
        const FString Flags = FString::Printf(TEXT("0x%08x"), Function.Flags);
        Functions.Items.push_back(J::Object({{"path", String(Function.Path)}, {"flags", String(Flags)},
            {"script_signature", J::Object({{"parameters", Parameters}, {"return_property", Return}})}}));
    }
    for (const FP4NativeReflectionStructure& Structure : Snapshot.Structures)
    {
        J Fields = J::Array();
        for (const FP4NativeReflectionProperty& Property : Structure.Fields)
            Fields.Items.push_back(J::Object({{"name", String(Property.Name)}, {"type_detail", Detail(Property)}}));
        Structures.Items.push_back(J::Object({{"path", String(Structure.Path)},
            {"super_path", Structure.SuperPath.IsEmpty() ? J() : String(Structure.SuperPath)}, {"fields", Fields}}));
    }
    for (const FP4NativeReflectionEnum& Enum : Snapshot.Enums)
    {
        J Values = J::Array();
        for (int32 Index = 0; Index < Enum.Values.Num(); ++Index)
        {
            J Value = J::Object({{"name", String(Enum.Values[Index])}});
            if (Enum.NumericValues.IsValidIndex(Index))
                Value["value"] = J(std::int64_t(Enum.NumericValues[Index]));
            Values.Items.push_back(MoveTemp(Value));
        }
        Enums.Items.push_back(
            J::Object({{"path", String(Enum.Path)}, {"values", Values}, {"cpp_form", int32(Enum.CppForm)},
                {"cpp_type", String(Enum.CppType)}, {"enum_flags", J(std::uint64_t(Enum.EnumFlags))}}));
    }
    return J::Object({{"functions", Functions}, {"structures", Structures}, {"enums", Enums}});
}

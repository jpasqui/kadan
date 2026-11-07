#pragma once
#include "CoreMinimal.h"
#include <utility>
#include <vector>

class FProperty;
class UClass;
class UEnum;

// The data of the Blender export (scene.json and the material files) in the form the TransMat add-on
// reads: Python's JSON values, written the way Python's json.dump(indent=1) writes them. Integers and
// floats stay apart, keys keep their order and floats are written in Python's shortest form.
struct KADAN_API FP4PyJson
{
    enum class EKind : uint8
    {
        Null,
        Bool,
        Int,
        Float,
        String,
        List,
        Dict
    };
    EKind Kind = EKind::Null;
    bool Bool = false;
    int64 Int = 0;
    double Float = 0.0;
    FString String;
    std::vector<FP4PyJson> Items;
    std::vector<std::pair<FString, FP4PyJson>> Fields;

    static FP4PyJson Null() { return FP4PyJson(); }
    static FP4PyJson Boolean(bool Value)
    {
        FP4PyJson V;
        V.Kind = EKind::Bool;
        V.Bool = Value;
        return V;
    }
    static FP4PyJson Integer(int64 Value)
    {
        FP4PyJson V;
        V.Kind = EKind::Int;
        V.Int = Value;
        return V;
    }
    static FP4PyJson Number(double Value)
    {
        FP4PyJson V;
        V.Kind = EKind::Float;
        V.Float = Value;
        return V;
    }
    static FP4PyJson Text(const FString& Value)
    {
        FP4PyJson V;
        V.Kind = EKind::String;
        V.String = Value;
        return V;
    }
    static FP4PyJson List()
    {
        FP4PyJson V;
        V.Kind = EKind::List;
        return V;
    }
    static FP4PyJson Dict()
    {
        FP4PyJson V;
        V.Kind = EKind::Dict;
        return V;
    }

    bool IsNull() const { return Kind == EKind::Null; }
    FP4PyJson* Find(const FString& Key);
    const FP4PyJson* Find(const FString& Key) const;
    // Python's d[key] = value: a key already there keeps its place.
    FP4PyJson& Set(const FString& Key, FP4PyJson Value);
    bool Remove(const FString& Key);
    FP4PyJson& Add(FP4PyJson Value)
    {
        Items.push_back(MoveTemp(Value));
        return Items.back();
    }
    // json.dumps(value, indent=1) of Python 3 (ensure_ascii, NaN and Infinity allowed).
    FString Dump() const;
};

namespace P4Blender
{
// repr() of a Python float.
KADAN_API FString PythonFloat(double Value);
// float() of Python on a string, false when Python raises ValueError.
KADAN_API bool PythonParseFloat(const FString& Text, double& Value);

// recomp/graphics/nvasm/material_t3d.py, parse(): an Unreal material's text export as the node graph
// {nodes, outputs}. Nodes keep the order of the text.
KADAN_API FP4PyJson ParseMaterialT3D(const FString& Text);
// material_t3d._link(): an input's value as [expression, channels(, output index)], or null.
KADAN_API FP4PyJson ParseLink(const FString& Value);
// {object name: its own stripped lines} of a text export (p4blender_ue._blocks).
KADAN_API TMap<FString, TArray<FString>> TextBlocks(const FString& Text);

// p4blender_ue.python_name(): bTwoSided -> two_sided, UTiling -> u_tiling.
KADAN_API FString KitPythonName(const FString& Name);
// The names Unreal's Python gives a property, an enum type and an enum entry.
KADAN_API FString PythonizeName(const FString& Name, bool Upper);
KADAN_API FString PropertyPythonName(const FProperty* Property);
KADAN_API FString EnumPythonName(const UEnum* Enum);
KADAN_API FString EnumEntryPythonName(const UEnum* Enum, int32 Index);
// What get_editor_property(PythonName) reads on an object of Class, or null when it raises.
KADAN_API const FProperty* EditorProperty(const UClass* Class, const FString& PythonName);
// p4blender_ue.encode() of what get_editor_property returns for Property of the object or struct at
// Container: null when the add-on has no form for it.
KADAN_API FP4PyJson EncodeProperty(const FProperty* Property, const void* Container);

// The TGA Unreal's texture exporter writes, as an 8-bit PNG with the TGA's channels (p4blender_ue.tga_to_png).
KADAN_API bool TgaToPng(const TArray<uint8>& Tga, TArray<uint8>& Png, FString& Error);
} //namespace P4Blender

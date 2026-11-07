// UE4.26 native data that follows the tagged properties of a cooked level's own objects: the level,
// its world, its BSP model and a Blueprint-generated class. Each reader takes the span from the end of
// the properties to the end of the export and has to use every byte of it. References are package
// indices as the file holds them: positive for an export, negative for an import, 0 for none.
#pragma once
#include "package_reader.h"

namespace P4MapKit
{
namespace LevelNative
{
// Every object's properties are followed by one flag that says whether a 16-byte id follows.
inline bool ObjectId(FByteReader& Reader)
{
    std::int32_t Has = 0;
    if (!Reader.I32(Has)) return false;
    if (Has != 0 && Has != 1) return Reader.Reject("invalid-object-id-flag");
    return Has == 0 || Reader.Skip(16);
}
inline bool References(FByteReader& Reader, std::vector<std::int32_t>& Out)
{
    std::size_t Count = 0;
    if (!Reader.Count(Count, 1000000)) return false;
    if (Count > (Reader.Limit() - Reader.Tell()) / 4) return Reader.Reject("truncated");
    Out.resize(Count);
    for (auto& Value : Out) if (!Reader.I32(Value)) return false;
    return true;
}
inline bool Flag(FByteReader& Reader, bool& Out)
{
    std::int32_t Value = 0;
    if (!Reader.I32(Value)) return false;
    if (Value != 0 && Value != 1) return Reader.Reject("invalid-flag");
    Out = Value == 1; return true;
}
// An array the engine writes in one piece: the size of an element, the count, the elements.
inline bool Bulk(FByteReader& Reader, std::size_t ElementSize, std::size_t& Count)
{
    std::int32_t Size = 0;
    if (!Reader.I32(Size) || !Reader.Count(Count, 100000000)) return false;
    if (Size < 0 || std::size_t(Size) != ElementSize) return Reader.Reject("unexpected-element-size");
    if (Count > (Reader.Limit() - Reader.Tell()) / ElementSize) return Reader.Reject("truncated");
    return Reader.Skip(Count * ElementSize);
}
inline bool Real(FByteReader& Reader, float& Out)
{
    std::uint32_t Bits = 0;
    if (!Reader.U32(Bits)) return false;
    std::memcpy(&Out, &Bits, 4); return true;
}
template <std::size_t Count> inline bool Reals(FByteReader& Reader, std::array<float, Count>& Out)
{
    for (float& Part : Out) if (!Real(Reader, Part)) return false;
    return true;
}
// The head of an array the engine writes in one piece: the size of an element, then the count.
inline bool BulkHead(FByteReader& Reader, std::size_t ElementSize, std::size_t& Count)
{
    std::int32_t Size = 0;
    if (!Reader.I32(Size) || !Reader.Count(Count, 100000000)) return false;
    if (Size < 0 || std::size_t(Size) != ElementSize) return Reader.Reject("unexpected-element-size");
    if (Count > (Reader.Limit() - Reader.Tell()) / ElementSize) return Reader.Reject("truncated");
    return true;
}
inline bool Done(FByteReader& Reader, std::size_t& End)
{
    if (!Reader.Error.Code.empty()) return false;
    if (Reader.Tell() != Reader.Limit()) return Reader.Reject("unread-native-bytes");
    End = Reader.Tell(); return true;
}
}

struct FLevelNative
{
    // The level's actors in the level's order. The cook leaves 0 where an actor was not kept.
    std::vector<std::int32_t> Actors;
    // The address the level was last saved under. The engine writes it, nobody authors it.
    std::string Protocol, Host, Map, Portal;
    std::vector<std::string> Options;
    std::int32_t Port = 0, Valid = 0;
    std::int32_t Model = 0, LevelScriptActor = 0, NavListStart = 0, NavListEnd = 0;
    std::vector<std::int32_t> ModelComponents;
    // Precomputed visibility cells and the precomputed distance field, both made by a lighting build.
    std::size_t VisibilityBuckets = 0, DistanceFieldValues = 0;
    std::size_t End = 0;
    FReadError Error;
};
inline FLevelNative ReadLevelNative(const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End)
{
    FLevelNative Out; FByteReader Reader(Body, Start, End);
    using namespace LevelNative;
    std::size_t Options = 0;
    bool Good = ObjectId(Reader) && References(Reader, Out.Actors) && Reader.String(Out.Protocol) && Reader.String(Out.Host) &&
        Reader.String(Out.Map) && Reader.String(Out.Portal) && Reader.Count(Options, 4096);
    for (std::size_t Index = 0; Good && Index < Options; ++Index)
    { Out.Options.emplace_back(); Good = Reader.String(Out.Options.back()); }
    Good = Good && Reader.I32(Out.Port) && Reader.I32(Out.Valid) && Reader.I32(Out.Model) && References(Reader, Out.ModelComponents) &&
        Reader.I32(Out.LevelScriptActor) && Reader.I32(Out.NavListStart) && Reader.I32(Out.NavListEnd) &&
        // Visibility: an origin, two cell sizes, the bucket size and count, then the buckets.
        Reader.Skip(24) && Reader.Count(Out.VisibilityBuckets, 100000000);
    if (Good && Out.VisibilityBuckets != 0) Good = Reader.Reject("precomputed-visibility-not-decoded");
    // Distance field: the largest distance, a box, three sizes, then the values.
    Good = Good && Reader.Skip(4 + 25 + 12) && Reader.Count(Out.DistanceFieldValues, 100000000);
    if (Good && Out.DistanceFieldValues > (Reader.Limit() - Reader.Tell()) / 4) Good = Reader.Reject("truncated");
    if (Good && Reader.Skip(Out.DistanceFieldValues * 4)) Done(Reader, Out.End);
    Out.Error = Reader.Error; return Out;
}

struct FWorldNative
{
    std::int32_t PersistentLevel = 0;
    std::vector<std::int32_t> ExtraReferencedObjects, StreamingLevels;
    std::size_t End = 0;
    FReadError Error;
};
inline FWorldNative ReadWorldNative(const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End)
{
    FWorldNative Out; FByteReader Reader(Body, Start, End);
    using namespace LevelNative;
    if (ObjectId(Reader) && Reader.I32(Out.PersistentLevel) && References(Reader, Out.ExtraReferencedObjects) &&
        References(Reader, Out.StreamingLevels)) Done(Reader, Out.End);
    Out.Error = Reader.Error; return Out;
}

// A BSP model: the level's own (empty when the level has no BSP geometry) or a brush's, which is the
// shape of a volume. A node is one flat polygon: a run of the vertex pool, on a surface.
struct FModelNode { std::array<float, 4> Plane{}; std::int32_t VertexPool = 0, Surface = 0; std::uint8_t Vertices = 0, Flags = 0; };
struct FModelSurface
{
    std::int32_t Material = 0, Base = 0, Normal = 0, TextureU = 0, TextureV = 0, BrushPolygon = 0, Actor = 0, LightmassIndex = 0;
    std::uint32_t PolyFlags = 0;
    std::array<float, 4> Plane{};
    float LightMapScale = 0;
};
struct FModelVertex { std::int32_t Point = 0, Side = 0; };
struct FModelLightmassSettings { std::array<std::uint32_t, 4> Flags{}; std::array<float, 5> Values{}; };
struct FModelNative
{
    std::array<float, 7> Bounds{};
    std::vector<std::array<float, 3>> Vectors, Points;
    std::vector<FModelNode> Nodes;
    std::vector<FModelSurface> Surfaces;
    std::vector<FModelVertex> Vertices;
    std::vector<FModelLightmassSettings> LightmassSettings;
    std::size_t BufferVertices = 0;
    std::int32_t SharedSides = 0, UniqueVertices = 0;
    bool RootOutside = false, Linked = false;
    std::array<std::uint32_t, 4> LightingGuid{};
    std::size_t End = 0;
    FReadError Error;
    // A model with no geometry: what a level that has no BSP holds.
    bool Empty() const { return Vectors.size() + Points.size() + Nodes.size() + Surfaces.size() + Vertices.size() + BufferVertices == 0; }
};
inline FModelNative ReadModelNative(const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End)
{
    FModelNative Out; FByteReader Reader(Body, Start, End);
    using namespace LevelNative;
    std::uint64_t Global = 0, Class = 0, Small = 0;
    std::size_t Count = 0;
    bool Good = ObjectId(Reader) && Reader.Unsigned(1, Global) && Reader.Unsigned(1, Class);
    // A cooked file has its editor data taken out, and that is the only layout read here.
    if (Good && ((Global & 1) == 0 || Global > 3 || Class > 1)) Good = Reader.Reject("model-not-cooked");
    Good = Good && Reals(Reader, Out.Bounds);
    for (auto* List : {&Out.Vectors, &Out.Points})
    {
        Good = Good && BulkHead(Reader, 12, Count);
        if (Good) List->resize(Count);
        for (std::size_t Index = 0; Good && Index < Count; ++Index) Good = Reals(Reader, (*List)[Index]);
    }
    Good = Good && BulkHead(Reader, 64, Count);
    if (Good) Out.Nodes.resize(Count);
    for (std::size_t Index = 0; Good && Index < Count; ++Index)
    {
        // The plane, where its vertices start in the pool and its surface. Then what a build of
        // the tree fills in: component indices, children, collision, zones, the count and leaves.
        FModelNode& Node = Out.Nodes[Index]; std::uint64_t Vertices = 0, Flags = 0;
        Good = Reals(Reader, Node.Plane) && Reader.I32(Node.VertexPool) && Reader.I32(Node.Surface) && Reader.Skip(4 + 2 + 2 + 4 + 12 + 4 + 2) &&
            Reader.Unsigned(1, Vertices) && Reader.Unsigned(1, Flags) && Reader.Skip(8);
        Node.Vertices = std::uint8_t(Vertices); Node.Flags = std::uint8_t(Flags);
    }
    Good = Good && Reader.Count(Count, 100000000);
    if (Good && Count > (Reader.Limit() - Reader.Tell()) / 56) Good = Reader.Reject("truncated");
    if (Good) Out.Surfaces.resize(Count);
    for (std::size_t Index = 0; Good && Index < Count; ++Index)
    {
        FModelSurface& Surface = Out.Surfaces[Index];
        Good = Reader.I32(Surface.Material) && Reader.U32(Surface.PolyFlags) && Reader.I32(Surface.Base) && Reader.I32(Surface.Normal) &&
            Reader.I32(Surface.TextureU) && Reader.I32(Surface.TextureV) && Reader.I32(Surface.BrushPolygon) && Reader.I32(Surface.Actor) &&
            Reals(Reader, Surface.Plane) && Real(Reader, Surface.LightMapScale) && Reader.I32(Surface.LightmassIndex);
    }
    Good = Good && BulkHead(Reader, 24, Count);
    if (Good) Out.Vertices.resize(Count);
    for (std::size_t Index = 0; Good && Index < Count; ++Index)
        Good = Reader.I32(Out.Vertices[Index].Point) && Reader.I32(Out.Vertices[Index].Side) && Reader.Skip(16);
    Good = Good && Reader.I32(Out.SharedSides) && Flag(Reader, Out.RootOutside) && Flag(Reader, Out.Linked) && Reader.I32(Out.UniqueVertices);
    // The vertex buffer is left out only of a brush's model.
    if (Good && (Class & 1) == 0)
    {
        Good = Reader.Count(Out.BufferVertices, 100000000);
        if (Good && Out.BufferVertices > (Reader.Limit() - Reader.Tell()) / 36) Good = Reader.Reject("truncated");
        Good = Good && Reader.Skip(Out.BufferVertices * 36);
    }
    for (auto& Part : Out.LightingGuid) Good = Good && Reader.U32(Part);
    // Lighting build settings a surface can point at: four flags and five numbers each.
    Good = Good && Reader.Count(Count, 100000000);
    if (Good && Count > (Reader.Limit() - Reader.Tell()) / 36) Good = Reader.Reject("truncated");
    if (Good) Out.LightmassSettings.resize(Count);
    for (std::size_t Index = 0; Good && Index < Count; ++Index)
    {
        for (auto& Part : Out.LightmassSettings[Index].Flags) Good = Good && Reader.U32(Part);
        Good = Good && Reals(Reader, Out.LightmassSettings[Index].Values);
    }
    (void)Small;
    if (Good) Done(Reader, Out.End);
    Out.Error = Reader.Error; return Out;
}

// What a static mesh component holds for each level of detail of its mesh.
struct FStaticMeshComponentNative
{
    std::size_t Lods = 0;
    // The id of each level of detail in the level's lighting build data. A lighting build makes them.
    std::vector<std::array<std::uint32_t, 4>> LightingIds;
    std::size_t End = 0;
    FReadError Error;
};
namespace LevelNative
{
inline bool ComponentLods(FByteReader& Reader, FStaticMeshComponentNative& Out)
{
    bool Good = ObjectId(Reader) && Reader.Count(Out.Lods, 64);
    for (std::size_t Lod = 0; Good && Lod < Out.Lods; ++Lod)
    {
        std::uint64_t Global = 0, Class = 0, Painted = 0;
        Good = Reader.Unsigned(1, Global) && Reader.Unsigned(1, Class);
        if (Good && (Global > 3 || Class > 1)) Good = Reader.Reject("invalid-strip-flags");
        // The lighting id is left out of a file made for a server.
        if (Good && (Global & 2) == 0)
        {
            std::array<std::uint32_t, 4> Id{};
            for (auto& Part : Id) Good = Good && Reader.U32(Part);
            if (Good) Out.LightingIds.push_back(Id);
        }
        // Vertex colors painted on this component, when the cook kept them.
        if (Good && (Class & 1) == 0)
        {
            Good = Reader.Unsigned(1, Painted);
            if (Good && Painted > 1) Good = Reader.Reject("invalid-flag");
            if (Good && Painted == 1) Good = Reader.Reject("painted-vertex-colors-not-decoded");
        }
    }
    return Good;
}
// A 16-bit real as the 32-bit real of the same value.
inline float Half(std::uint16_t Bits)
{
    const std::uint32_t Sign = std::uint32_t(Bits & 0x8000u) << 16, Exponent = (Bits >> 10) & 0x1fu;
    std::uint32_t Fraction = Bits & 0x3ffu, Wide = 0;
    if (Exponent == 0x1f) Wide = Sign | 0x7f800000u | (Fraction << 13);
    else if (Exponent != 0) Wide = Sign | ((Exponent + 112) << 23) | (Fraction << 13);
    else if (Fraction != 0)
    {
        std::uint32_t Shift = 0;
        while ((Fraction & 0x400u) == 0) { Fraction <<= 1; ++Shift; }
        Wide = Sign | ((113 - Shift) << 23) | ((Fraction & 0x3ffu) << 13);
    }
    else Wide = Sign;
    float Out = 0; std::memcpy(&Out, &Wide, 4); return Out;
}
// The head of a buffer of vertex data: the size of an element, then the count.
inline bool Stream(FByteReader& Reader, std::size_t ElementSize, std::size_t Wanted)
{
    std::size_t Count = 0;
    if (!BulkHead(Reader, ElementSize, Count)) return false;
    return Count == Wanted || Reader.Reject("unexpected-instance-count");
}
}
inline FStaticMeshComponentNative ReadStaticMeshComponentNative(const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End)
{
    FStaticMeshComponentNative Out; FByteReader Reader(Body, Start, End);
    if (LevelNative::ComponentLods(Reader, Out)) LevelNative::Done(Reader, Out.End);
    Out.Error = Reader.Error; return Out;
}

// A node of the tree a hierarchical instanced mesh component culls its instances by.
struct FInstanceCluster
{
    std::array<float, 3> Min{}, Max{}, MinScale{}, MaxScale{};
    std::int32_t FirstChild = 0, LastChild = 0, FirstInstance = 0, LastInstance = 0;
};
// What an instanced static mesh component holds: where each instance is, what the engine built from
// that to draw them, and for a hierarchical one the tree it culls them by.
struct FInstancedMeshNative
{
    FStaticMeshComponentNative Mesh;
    bool Cooked = false, Hierarchical = false;
    // Each instance's place, as the four rows of its matrix, and the numbers of its own that it gives
    // its material.
    std::vector<std::array<float, 16>> Instances;
    std::vector<float> Custom;
    // What is drawn, in the order it is drawn in. 0 bytes when the cook kept none of it.
    std::uint64_t DrawnBytes = 0;
    bool HalfReals = false;
    std::int32_t Drawn = 0, CustomPerInstance = 0;
    // Where it is drawn, with the instance's random number last.
    std::vector<std::array<float, 4>> Origins;
    // Its place in the light map and the shadow map of a lighting build.
    std::vector<std::array<std::int16_t, 4>> LightMaps;
    // Three rows of its matrix, each with a fourth number for its editor.
    std::vector<std::array<float, 12>> Rows;
    std::vector<float> DrawnCustom;
    std::vector<FInstanceCluster> Clusters;
    std::size_t End = 0;
    FReadError Error;
};
inline FInstancedMeshNative ReadInstancedMeshNative(const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End, bool Hierarchical)
{
    FInstancedMeshNative Out; FByteReader Reader(Body, Start, End);
    using namespace LevelNative;
    Out.Hierarchical = Hierarchical;
    std::size_t Count = 0;
    bool Good = ComponentLods(Reader, Out.Mesh) && Flag(Reader, Out.Cooked) && BulkHead(Reader, 64, Count);
    if (Good) Out.Instances.resize(Count);
    for (std::size_t Index = 0; Good && Index < Count; ++Index) Good = Reals(Reader, Out.Instances[Index]);
    Good = Good && BulkHead(Reader, 4, Count);
    if (Good) Out.Custom.resize(Count);
    for (std::size_t Index = 0; Good && Index < Count; ++Index) Good = Real(Reader, Out.Custom[Index]);
    if (Good && Out.Cooked)
    {
        Good = Reader.Unsigned(8, Out.DrawnBytes);
        const std::size_t Begin = Reader.Tell();
        if (Good && Out.DrawnBytes > Reader.Limit() - Begin) Good = Reader.Reject("truncated");
        if (Good && Out.DrawnBytes)
        {
            Good = Flag(Reader, Out.HalfReals) && Reader.I32(Out.Drawn) && Reader.I32(Out.CustomPerInstance);
            if (Good && (Out.Drawn < 0 || Out.CustomPerInstance < 0 || Out.CustomPerInstance > 4096)) Good = Reader.Reject("invalid-count");
            const std::size_t Drawn = Good ? std::size_t(Out.Drawn) : 0;
            Good = Good && Stream(Reader, 16, Drawn);
            if (Good) Out.Origins.resize(Drawn);
            for (std::size_t Index = 0; Good && Index < Drawn; ++Index) Good = Reals(Reader, Out.Origins[Index]);
            Good = Good && Stream(Reader, 8, Drawn);
            if (Good) Out.LightMaps.resize(Drawn);
            for (std::size_t Index = 0; Good && Index < Drawn; ++Index)
                for (std::int16_t& Part : Out.LightMaps[Index])
                {
                    std::uint64_t Bits = 0;
                    Good = Good && Reader.Unsigned(2, Bits);
                    Part = std::int16_t(std::uint16_t(Bits));
                }
            Good = Good && Stream(Reader, Out.HalfReals ? 24 : 48, Drawn);
            if (Good) Out.Rows.resize(Drawn);
            for (std::size_t Index = 0; Good && Index < Drawn; ++Index)
                for (float& Part : Out.Rows[Index])
                {
                    std::uint64_t Bits = 0;
                    if (!Out.HalfReals) { Good = Good && Real(Reader, Part); continue; }
                    Good = Good && Reader.Unsigned(2, Bits);
                    Part = Half(std::uint16_t(Bits));
                }
            Good = Good && Stream(Reader, 4, Drawn * std::size_t(Out.CustomPerInstance));
            if (Good) Out.DrawnCustom.resize(Drawn * std::size_t(Out.CustomPerInstance));
            for (std::size_t Index = 0; Good && Index < Out.DrawnCustom.size(); ++Index) Good = Real(Reader, Out.DrawnCustom[Index]);
            if (Good && Reader.Tell() - Begin != Out.DrawnBytes) Good = Reader.Reject("unexpected-drawn-size");
        }
    }
    if (Good && Hierarchical)
    {
        Good = BulkHead(Reader, 64, Count);
        if (Good) Out.Clusters.resize(Count);
        for (std::size_t Index = 0; Good && Index < Count; ++Index)
        {
            FInstanceCluster& Node = Out.Clusters[Index];
            Good = Reals(Reader, Node.Min) && Reader.I32(Node.FirstChild) && Reals(Reader, Node.Max) && Reader.I32(Node.LastChild) &&
                Reader.I32(Node.FirstInstance) && Reader.I32(Node.LastInstance) && Reals(Reader, Node.MinScale) && Reals(Reader, Node.MaxScale);
        }
    }
    if (Good) Done(Reader, Out.End);
    Out.Error = Reader.Error; return Out;
}

// What a level's foliage actor holds: for each kind of foliage, the component that draws it.
struct FFoliagePlaced
{
    std::int32_t Type = 0, Component = 0;
};
struct FFoliageActorNative
{
    std::vector<FFoliagePlaced> Placed;
    std::size_t End = 0;
    FReadError Error;
};
inline FFoliageActorNative ReadFoliageActorNative(const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End)
{
    FFoliageActorNative Out; FByteReader Reader(Body, Start, End);
    using namespace LevelNative;
    std::size_t Count = 0;
    bool Good = ObjectId(Reader) && Reader.Count(Count, 100000);
    if (Good && Count > (Reader.Limit() - Reader.Tell()) / 9) Good = Reader.Reject("truncated");
    for (std::size_t Index = 0; Good && Index < Count; ++Index)
    {
        FFoliagePlaced Entry; std::uint64_t Kind = 0;
        Good = Reader.I32(Entry.Type) && Reader.Unsigned(1, Kind);
        // 1 is foliage of instanced meshes. Foliage of whole actors is laid out otherwise.
        if (Good && Kind != 1) Good = Reader.Reject("foliage-kind-not-decoded");
        Good = Good && Reader.I32(Entry.Component);
        if (Good) Out.Placed.push_back(Entry);
    }
    if (Good) Done(Reader, Out.End);
    Out.Error = Reader.Error; return Out;
}

// A variable of a Blueprint-generated class, as the class holds it.
struct FClassVariable
{
    std::string Type, Name;
    std::uint32_t ObjectFlags = 0;
    std::int32_t ArrayDim = 0, ElementSize = 0;
    std::uint64_t PropertyFlags = 0;
    std::string RepNotify;
};
struct FGeneratedClassNative
{
    std::int32_t Super = 0, Within = 0, GeneratedBy = 0, DefaultObject = 0;
    std::vector<std::int32_t> Children;
    std::vector<FClassVariable> Variables;
    std::vector<std::pair<std::string, std::int32_t>> Functions;
    std::size_t ScriptBytes = 0, Interfaces = 0;
    std::uint32_t ClassFlags = 0;
    std::string ConfigName;
    bool Cooked = false;
    // The type of the first variable this reader cannot read, when that is why it stopped.
    std::string UndecodedType;
    bool UnsupportedVariable = false;
    std::size_t End = 0;
    FReadError Error;
};
inline FGeneratedClassNative ReadGeneratedClassNative(const std::vector<std::uint8_t>& Body, const std::vector<std::string>& Names,
    std::size_t Start, std::size_t End)
{
    FGeneratedClassNative Out; FByteReader Reader(Body, Start, End);
    using namespace LevelNative;
    // Variable types that hold nothing besides what every variable holds.
    static const char* const Plain[] = {"Int8Property", "Int16Property", "IntProperty", "Int64Property", "UInt16Property",
        "UInt32Property", "UInt64Property", "FloatProperty", "DoubleProperty", "NameProperty", "StrProperty", "TextProperty"};
    std::size_t Variables = 0, Functions = 0;
    bool Good = ObjectId(Reader) && Reader.I32(Out.Super) && References(Reader, Out.Children) && Reader.Count(Variables, 100000);
    for (std::size_t Index = 0; Good && Index < Variables; ++Index)
    {
        FClassVariable Variable; std::uint64_t Flags = 0, Small = 0;
        Good = Reader.Name(Names, Variable.Type);
        if (Good && std::find_if(std::begin(Plain), std::end(Plain), [&](const char* Known) { return Variable.Type == Known; }) == std::end(Plain))
        { Out.UndecodedType = Variable.Type; Out.UnsupportedVariable = true; Good = Reader.Reject("class-variable-type-not-decoded"); }
        // Name and object flags, then the size of the array, the size of an element, the property
        // flags, the replication index, the function to call on replication and its condition.
        Good = Good && Reader.Name(Names, Variable.Name) && Reader.U32(Variable.ObjectFlags) && Reader.I32(Variable.ArrayDim) &&
            Reader.I32(Variable.ElementSize) && Reader.Unsigned(8, Flags) && Reader.Unsigned(2, Small) &&
            Reader.Name(Names, Variable.RepNotify) && Reader.Unsigned(1, Small);
        Variable.PropertyFlags = Flags;
        if (Good) Out.Variables.push_back(std::move(Variable));
    }
    std::int32_t Bytecode = 0, Script = 0;
    Good = Good && Reader.I32(Bytecode) && Reader.I32(Script);
    if (Good && (Bytecode < 0 || Script < 0)) Good = Reader.Reject("invalid-count");
    if (Good) { Out.ScriptBytes = std::size_t(Script); Good = Reader.Skip(Out.ScriptBytes); }
    Good = Good && Reader.Count(Functions, 100000);
    for (std::size_t Index = 0; Good && Index < Functions; ++Index)
    {
        std::string Name; std::int32_t Function = 0;
        Good = Reader.Name(Names, Name) && Reader.I32(Function);
        if (Good) Out.Functions.emplace_back(std::move(Name), Function);
    }
    bool Order = false; std::string Dummy;
    Good = Good && Reader.U32(Out.ClassFlags) && Reader.I32(Out.Within) && Reader.Name(Names, Out.ConfigName) && Reader.I32(Out.GeneratedBy) &&
        Reader.Count(Out.Interfaces, 100000);
    // An interface is its class, an offset and a flag.
    if (Good && Out.Interfaces > (Reader.Limit() - Reader.Tell()) / 12) Good = Reader.Reject("truncated");
    if (Good && Reader.Skip(Out.Interfaces * 12) && Flag(Reader, Order) && Reader.Name(Names, Dummy) && Flag(Reader, Out.Cooked) &&
        Reader.I32(Out.DefaultObject)) Done(Reader, Out.End);
    Out.Error = Reader.Error; return Out;
}
}

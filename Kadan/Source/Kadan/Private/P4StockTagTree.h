// Tagged properties as a tree, read from one package's bytes and written into another's. The game adds
// members to some of the engine's own structs and classes (a notify's middle timing, a skeletal mesh
// level's C4Distance). This engine has no property for them, so the editor keeps the game's own tags
// and the cook writes them back where the game has them. Every name is carried as a name, an object
// reference only when it is empty, and a payload the reader does not know refuses.
#pragma once
#include "P4MapKitCore/package_reader.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <array>
#include <set>
#include <string>
#include <vector>

namespace P4StockTags
{
using Bytes = std::vector<std::uint8_t>;
struct FNameWord
{
    std::string Base;
    std::int32_t Number = 0;
};
inline bool SameWord(const FNameWord& A, const FNameWord& B)
{
    if (A.Number != B.Number || A.Base.size() != B.Base.size())
        return false;
    for (std::size_t At = 0; At < A.Base.size(); ++At)
        if (std::tolower(std::uint8_t(A.Base[At])) != std::tolower(std::uint8_t(B.Base[At])))
            return false;
    return true;
}
struct FNode;
using FList = std::vector<FNode>;
struct FNode
{
    enum class EForm
    {
        Raw,
        Name,
        Tagged,
        Array,
        Reference
    };
    FNameWord Name, Type;
    std::vector<FNameWord> Detail;
    std::uint32_t ArrayIndex = 0;
    bool BoolValue = false, HasGuid = false;
    std::array<std::uint32_t, 4> StructGuid{}, PropertyGuid{};
    EForm Form = EForm::Raw;
    Bytes Raw;
    FNameWord Value;
    FList Children;
    // A reference, by the referenced object's path (package-relative for the package's own objects).
    std::string ReferencePath;
    // Where the tag starts in the bytes it was read from (none for a tag made here).
    std::size_t At = 0;
    // An array: its count, and for structs the inner descriptor and each element.
    std::int32_t Count = 0;
    bool StructArray = false, NameArray = false;
    FNameWord InnerName, InnerStruct;
    std::array<std::uint32_t, 4> InnerStructGuid{}, InnerPropertyGuid{};
    bool InnerHasGuid = false;
    std::uint32_t InnerArrayIndex = 0;
    std::vector<FList> Elements;
    std::vector<Bytes> RawElements;
    std::vector<FNameWord> NameElements;
};

// Structs the engine writes natively that hold no names: copied as bytes.
inline bool RawStruct(const std::string& Name)
{
    static const std::set<std::string> Known{"PerPlatformFloat", "PerPlatformInt", "PerPlatformBool", "Vector",
        "Vector2D", "Vector4", "Rotator", "Quat", "Color", "LinearColor", "Guid", "IntPoint", "IntVector", "Box",
        "Box2D", "Plane", "Matrix", "Transform", "FloatRange", "Int32Range", "FrameNumber", "Timespan", "DateTime"};
    return Known.count(Name) != 0;
}
inline bool PlainType(const std::string& Type)
{
    static const std::set<std::string> Plain{"IntProperty", "Int8Property", "Int16Property", "Int64Property",
        "UInt16Property", "UInt32Property", "UInt64Property", "FloatProperty", "DoubleProperty", "StrProperty",
        "TextProperty"};
    return Plain.count(Type) != 0;
}
inline bool ReferenceType(const std::string& Type)
{
    return Type == "ObjectProperty" || Type == "ClassProperty" || Type == "InterfaceProperty" ||
        Type == "WeakObjectProperty" || Type == "LazyObjectProperty";
}

// Strict (from the game's package into another): only what is known is carried, names by name.
// Same package (rewriting a package's own export): what is not descended into is kept as its bytes,
// which stay right because the package's names are only ever appended to.
struct FReader
{
    const Bytes& Body;
    const std::vector<std::string>& Names;
    bool SamePackage = false;
    std::string Error;
    // Strict reading of a package: a non-empty reference is carried by path when this resolves it.
    std::function<bool(std::int32_t, std::string&)> ReferenceToPath;
    // A record of one tag in names of its own carries a reference as a name holding its path.
    bool LocalReferences = false;
    bool Fail(const std::string& Why)
    {
        if (Error.empty())
            Error = Why;
        return false;
    }
    bool U32(std::size_t& At, std::size_t End, std::uint32_t& Out)
    {
        if (At + 4 > End)
            return Fail("stock-tag-truncated");
        Out = std::uint32_t(Body[At]) | (std::uint32_t(Body[At + 1]) << 8) | (std::uint32_t(Body[At + 2]) << 16) |
            (std::uint32_t(Body[At + 3]) << 24);
        At += 4;
        return true;
    }
    bool Word(std::size_t& At, std::size_t End, FNameWord& Out)
    {
        std::uint32_t Index = 0, Number = 0;
        if (!U32(At, End, Index) || !U32(At, End, Number))
            return false;
        if (Index >= Names.size() || std::int32_t(Number) < 0)
            return Fail("stock-tag-invalid-name");
        Out.Base = Names[Index];
        Out.Number = std::int32_t(Number);
        return true;
    }
    bool List(std::size_t& At, std::size_t End, FList& Out, int Depth)
    {
        if (Depth > 12)
            return Fail("stock-tag-too-deep");
        for (int Count = 0; Count < 100000; ++Count)
        {
            std::size_t Look = At;
            FNameWord First;
            if (!Word(Look, End, First))
                return false;
            if (First.Base == "None" && First.Number == 0)
            {
                At = Look;
                return true;
            }
            FNode Node;
            if (!One(At, End, Node, Depth))
                return false;
            Out.push_back(std::move(Node));
        }
        return Fail("stock-tag-too-many");
    }
    bool One(std::size_t& At, std::size_t End, FNode& Node, int Depth)
    {
        std::uint32_t Size = 0, Index = 0;
        Node.At = At;
        if (!Word(At, End, Node.Name) || !Word(At, End, Node.Type) || !U32(At, End, Size) || !U32(At, End, Index))
            return false;
        Node.ArrayIndex = Index;
        const std::string& Type = Node.Type.Base;
        int Details = 0;
        if (Type == "StructProperty" || Type == "ByteProperty" || Type == "EnumProperty" || Type == "ArrayProperty" ||
            Type == "SetProperty")
            Details = 1;
        else if (Type == "MapProperty")
            Details = 2;
        else if (Type == "BoolProperty")
        {
            if (At + 1 > End || Body[At] > 1 || Size != 0)
                return Fail("stock-tag-invalid-bool");
            Node.BoolValue = Body[At++] != 0;
        }
        else if (!SamePackage && !PlainType(Type) && Type != "NameProperty" && !ReferenceType(Type))
            return Fail("stock-tag-type-not-carried: " + Type);
        for (int N = 0; N < Details; ++N)
        {
            FNameWord Detail;
            if (!Word(At, End, Detail))
                return false;
            Node.Detail.push_back(Detail);
        }
        if (Type == "StructProperty")
            for (auto& Word_ : Node.StructGuid)
                if (!U32(At, End, Word_))
                    return false;
        if (At + 1 > End || Body[At] > 1)
            return Fail("stock-tag-invalid-guid-flag");
        Node.HasGuid = Body[At++] != 0;
        if (Node.HasGuid)
            for (auto& Word_ : Node.PropertyGuid)
                if (!U32(At, End, Word_))
                    return false;
        const std::size_t Start = At, Stop = At + Size;
        if (Stop > End)
            return Fail("stock-tag-payload-truncated");
        At = Stop;
        if (Type == "BoolProperty")
            return true;
        if (SamePackage)
        {
            // Descend only into tagged structs and arrays of them, which a path can name; keep the rest.
            Node.Raw.assign(Body.begin() + Start, Body.begin() + Stop);
            if (Type == "StructProperty" && !RawStruct(Node.Detail[0].Base))
            {
                FReader Inner{Body, Names, true};
                std::size_t Here = Start;
                FList Children;
                if (Inner.List(Here, Stop, Children, Depth + 1) && Here == Stop)
                {
                    Node.Form = FNode::EForm::Tagged;
                    Node.Children = std::move(Children);
                    Node.Raw.clear();
                }
                return true;
            }
            if (Type == "ArrayProperty" && Node.Detail[0].Base == "StructProperty")
            {
                FReader Inner{Body, Names, true};
                FNode Parsed = Node;
                std::size_t Here = Start;
                if (Inner.StructArray(Here, Stop, Parsed, Depth) && Here == Stop && Parsed.RawElements.empty())
                {
                    Parsed.Raw.clear();
                    Node = std::move(Parsed);
                }
            }
            return true;
        }
        if (PlainType(Type))
        {
            Node.Raw.assign(Body.begin() + Start, Body.begin() + Stop);
            return true;
        }
        if (ReferenceType(Type))
        {
            if (LocalReferences && Size == 8)
            {
                std::size_t Here = Start;
                FNameWord Path;
                if (!Word(Here, Stop, Path))
                    return false;
                Node.Form = FNode::EForm::Reference;
                Node.ReferencePath = Path.Base;
                return true;
            }
            if (Size != 4)
                return Fail("stock-tag-object-reference-size");
            const std::int32_t Referenced = std::int32_t(std::uint32_t(Body[Start]) |
                (std::uint32_t(Body[Start + 1]) << 8) | (std::uint32_t(Body[Start + 2]) << 16) |
                (std::uint32_t(Body[Start + 3]) << 24));
            if (Referenced == 0)
            {
                Node.Raw.assign(Body.begin() + Start, Body.begin() + Stop);
                return true;
            }
            if (!ReferenceToPath || !ReferenceToPath(Referenced, Node.ReferencePath) || Node.ReferencePath.empty())
                return Fail("stock-tag-object-reference-not-carried");
            Node.Form = FNode::EForm::Reference;
            return true;
        }
        if (Type == "NameProperty" || Type == "EnumProperty" ||
            (Type == "ByteProperty" && Node.Detail[0].Base != "None"))
        {
            std::size_t Here = Start;
            Node.Form = FNode::EForm::Name;
            if (Size != 8 || !Word(Here, Stop, Node.Value))
                return Fail("stock-tag-name-payload-invalid");
            return true;
        }
        if (Type == "ByteProperty")
        {
            if (Size != 1)
                return Fail("stock-tag-byte-payload-invalid");
            Node.Raw.assign(Body.begin() + Start, Body.begin() + Stop);
            return true;
        }
        if (Type == "StructProperty")
        {
            if (RawStruct(Node.Detail[0].Base))
            {
                Node.Raw.assign(Body.begin() + Start, Body.begin() + Stop);
                return true;
            }
            std::size_t Here = Start;
            Node.Form = FNode::EForm::Tagged;
            if (!List(Here, Stop, Node.Children, Depth + 1))
                return false;
            return Here == Stop || Fail("stock-tag-struct-size-differs");
        }
        // Map and set payloads have their own removal/key layouts. Keeping their type
        // descriptor does not make them arrays; strict cross-package carry refuses them.
        if (Type != "ArrayProperty")
            return Fail("stock-tag-type-not-carried: " + Type);
        // ArrayProperty
        Node.Form = FNode::EForm::Array;
        std::size_t Here = Start;
        std::uint32_t Count = 0;
        if (!U32(Here, Stop, Count) || Count > 100000)
            return Fail("stock-tag-array-count-invalid");
        Node.Count = std::int32_t(Count);
        const std::string& Inner = Node.Detail[0].Base;
        if (Inner == "StructProperty")
        {
            Here = Start;
            return StructArray(Here, Stop, Node, Depth) &&
                (Here == Stop || Fail("stock-tag-struct-array-elements-size"));
        }
        if (Inner == "NameProperty" || Inner == "EnumProperty")
        {
            Node.NameArray = true;
            for (std::uint32_t N = 0; N < Count; ++N)
            {
                FNameWord Item;
                if (!Word(Here, Stop, Item))
                    return false;
                Node.NameElements.push_back(Item);
            }
            return Here == Stop || Fail("stock-tag-name-array-size");
        }
        if (PlainType(Inner) || Inner == "ByteProperty" || Inner == "BoolProperty")
        {
            Node.Raw.assign(Body.begin() + Here, Body.begin() + Stop);
            return true;
        }
        if (ReferenceType(Inner))
        {
            for (std::size_t Byte = Here; Byte < Stop; ++Byte)
                if (Body[Byte])
                    return Fail("stock-tag-object-reference-not-carried");
            Node.Raw.assign(Body.begin() + Here, Body.begin() + Stop);
            return true;
        }
        return Fail("stock-tag-array-type-not-carried: " + Inner);
    }
    bool StructArray(std::size_t& Here, std::size_t Stop, FNode& Node, int Depth)
    {
        std::uint32_t Count = 0;
        if (!U32(Here, Stop, Count) || Count > 100000)
            return Fail("stock-tag-array-count-invalid");
        Node.Form = FNode::EForm::Array;
        Node.Count = std::int32_t(Count);
        Node.StructArray = true;
        FNameWord InnerType;
        std::uint32_t InnerSize = 0, InnerIndex = 0;
        if (!Word(Here, Stop, Node.InnerName) || !Word(Here, Stop, InnerType) || InnerType.Base != "StructProperty" ||
            !U32(Here, Stop, InnerSize) || !U32(Here, Stop, InnerIndex) || !Word(Here, Stop, Node.InnerStruct))
            return Fail("stock-tag-struct-array-header-invalid");
        Node.InnerArrayIndex = InnerIndex;
        for (auto& Word_ : Node.InnerStructGuid)
            if (!U32(Here, Stop, Word_))
                return false;
        if (Here + 1 > Stop || Body[Here] > 1)
            return Fail("stock-tag-invalid-guid-flag");
        Node.InnerHasGuid = Body[Here++] != 0;
        if (Node.InnerHasGuid)
            for (auto& Word_ : Node.InnerPropertyGuid)
                if (!U32(Here, Stop, Word_))
                    return false;
        if (Here + InnerSize != Stop)
            return Fail("stock-tag-struct-array-size-differs");
        if (RawStruct(Node.InnerStruct.Base))
        {
            if (!Count)
                return InnerSize == 0 || Fail("stock-tag-struct-array-element-size");
            if (InnerSize % Count)
                return Fail("stock-tag-struct-array-element-size");
            const std::size_t Each = InnerSize / Count;
            for (std::uint32_t N = 0; N < Count; ++N)
            {
                Node.RawElements.emplace_back(Body.begin() + Here, Body.begin() + Here + Each);
                Here += Each;
            }
            return true;
        }
        for (std::uint32_t N = 0; N < Count; ++N)
        {
            FList Element;
            if (!List(Here, Stop, Element, Depth + 1))
                return false;
            Node.Elements.push_back(std::move(Element));
        }
        return true;
    }
};

struct FWriter
{
    std::vector<std::string>& Names;
    std::string Error;
    // Writing into a package: a reference's path becomes that package's own index for the object.
    std::function<bool(const std::string&, std::int32_t&)> PathToReference;
    bool LocalReferences = false;
    bool Fail(const std::string& Why)
    {
        if (Error.empty())
            Error = Why;
        return false;
    }
    static void U32(Bytes& Out, std::uint32_t Value)
    {
        for (int N = 0; N < 4; ++N)
            Out.push_back(std::uint8_t(Value >> (N * 8)));
    }
    static void Put(Bytes& Out, std::size_t At, std::uint32_t Value)
    {
        for (int N = 0; N < 4; ++N)
            Out[At + N] = std::uint8_t(Value >> (N * 8));
    }
    bool Word(Bytes& Out, const FNameWord& Name)
    {
        if (Name.Base.empty() || Name.Base.size() > 1024)
            return Fail("stock-tag-name-invalid");
        auto It = std::find(Names.begin(), Names.end(), Name.Base);
        if (It == Names.end())
        {
            Names.push_back(Name.Base);
            It = Names.end() - 1;
        }
        U32(Out, std::uint32_t(It - Names.begin()));
        U32(Out, std::uint32_t(Name.Number));
        return true;
    }
    bool List(Bytes& Out, const FList& Nodes)
    {
        for (const FNode& Node : Nodes)
            if (!One(Out, Node))
                return false;
        return Word(Out, FNameWord{"None", 0});
    }
    bool One(Bytes& Out, const FNode& Node)
    {
        if (!Word(Out, Node.Name) || !Word(Out, Node.Type))
            return false;
        const std::size_t SizeAt = Out.size();
        U32(Out, 0);
        U32(Out, Node.ArrayIndex);
        if (Node.Type.Base == "BoolProperty")
            Out.push_back(Node.BoolValue ? 1 : 0);
        for (const auto& Detail : Node.Detail)
            if (!Word(Out, Detail))
                return false;
        if (Node.Type.Base == "StructProperty")
            for (auto Word_ : Node.StructGuid)
                U32(Out, Word_);
        Out.push_back(Node.HasGuid ? 1 : 0);
        if (Node.HasGuid)
            for (auto Word_ : Node.PropertyGuid)
                U32(Out, Word_);
        const std::size_t Start = Out.size();
        switch (Node.Form)
        {
            case FNode::EForm::Raw:
                Out.insert(Out.end(), Node.Raw.begin(), Node.Raw.end());
                break;
            case FNode::EForm::Name:
                if (!Word(Out, Node.Value))
                    return false;
                break;
            case FNode::EForm::Reference:
            {
                if (LocalReferences)
                {
                    if (!Word(Out, FNameWord{Node.ReferencePath, 0}))
                        return false;
                    break;
                }
                std::int32_t Index = 0;
                if (!PathToReference || !PathToReference(Node.ReferencePath, Index) || !Index)
                    return Fail("stock-tag-reference-not-in-the-package: " + Node.ReferencePath);
                U32(Out, std::uint32_t(Index));
                break;
            }
            case FNode::EForm::Tagged:
                if (!List(Out, Node.Children))
                    return false;
                break;
            case FNode::EForm::Array:
            {
                U32(Out, std::uint32_t(Node.Count));
                if (Node.StructArray)
                {
                    if (!Word(Out, Node.InnerName) || !Word(Out, FNameWord{"StructProperty", 0}))
                        return false;
                    const std::size_t InnerAt = Out.size();
                    U32(Out, 0);
                    U32(Out, Node.InnerArrayIndex);
                    if (!Word(Out, Node.InnerStruct))
                        return false;
                    for (auto Word_ : Node.InnerStructGuid)
                        U32(Out, Word_);
                    Out.push_back(Node.InnerHasGuid ? 1 : 0);
                    if (Node.InnerHasGuid)
                        for (auto Word_ : Node.InnerPropertyGuid)
                            U32(Out, Word_);
                    const std::size_t Elements = Out.size();
                    if (!Node.RawElements.empty() || Node.Elements.empty())
                        for (const Bytes& Element : Node.RawElements)
                            Out.insert(Out.end(), Element.begin(), Element.end());
                    for (const FList& Element : Node.Elements)
                        if (!List(Out, Element))
                            return false;
                    Put(Out, InnerAt, std::uint32_t(Out.size() - Elements));
                }
                else if (Node.NameArray)
                {
                    for (const auto& Item : Node.NameElements)
                        if (!Word(Out, Item))
                            return false;
                }
                else
                    Out.insert(Out.end(), Node.Raw.begin(), Node.Raw.end());
                break;
            }
        }
        Put(Out, SizeAt, std::uint32_t(Out.size() - Start));
        return true;
    }
};

// Paths as the import tells them: "Notifies[3].MiddleTriggerTimeOffset", "LODInfo[0].C4Distance", "bUseOneEmitter".
struct FStep
{
    std::string Name;
    int Index = -1;
};
inline bool ParsePath(const std::string& Path, std::vector<FStep>& Out)
{
    Out.clear();
    std::size_t At = 0;
    while (At < Path.size())
    {
        const std::size_t Dot = Path.find('.', At);
        std::string Part = Path.substr(At, Dot == std::string::npos ? std::string::npos : Dot - At);
        FStep Step;
        const std::size_t Open = Part.find('[');
        if (Open != std::string::npos)
        {
            if (Part.back() != ']' || Open + 2 > Part.size() - 1)
                return false;
            const std::string Digits = Part.substr(Open + 1, Part.size() - Open - 2);
            if (Digits.empty() || Digits.size() > 6 || Digits.find_first_not_of("0123456789") != std::string::npos)
                return false;
            Step.Index = std::stoi(Digits);
            Part = Part.substr(0, Open);
        }
        if (Part.empty())
            return false;
        Step.Name = Part;
        Out.push_back(Step);
        if (Dot == std::string::npos)
            break;
        At = Dot + 1;
    }
    return !Out.empty();
}
inline FNode* Find(FList& List, const std::string& Name)
{
    for (FNode& Node : List)
        if (Node.ArrayIndex == 0 && SameWord(Node.Name, FNameWord{Name, 0}))
            return &Node;
    return nullptr;
}
inline int FindAt(const FList& List, const std::string& Name)
{
    for (std::size_t At = 0; At < List.size(); ++At)
        if (List[At].ArrayIndex == 0 && SameWord(List[At].Name, FNameWord{Name, 0}))
            return int(At);
    return -1;
}
// The list a path's last step lives in. Array elements are TArray elements of structs.
inline FList* Parent(FList& Root, const std::vector<FStep>& Steps)
{
    FList* List = &Root;
    for (std::size_t At = 0; At + 1 < Steps.size(); ++At)
    {
        FNode* Node = Find(*List, Steps[At].Name);
        if (!Node)
            return nullptr;
        if (Steps[At].Index >= 0)
        {
            if (Node->Form != FNode::EForm::Array || !Node->StructArray ||
                Steps[At].Index >= int(Node->Elements.size()))
                return nullptr;
            List = &Node->Elements[std::size_t(Steps[At].Index)];
        }
        else
        {
            if (Node->Form != FNode::EForm::Tagged)
                return nullptr;
            List = &Node->Children;
        }
    }
    return Steps.back().Index >= 0 ? nullptr : List;
}
} //namespace P4StockTags

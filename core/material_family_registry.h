// Source metadata only. A family row is never an editable/imported asset.
#pragma once
#include "package_reader.h"
#include <algorithm>
#include <map>
#include <set>

namespace P4MapKit
{
namespace Material
{
struct FMaterialParentRow
{
    std::string Package, Object, Class, ParentPackage, ParentObject, ParentClass;
};
struct FMaterialRegistryFamily
{
    FMaterialParentRow Master;
    std::vector<FMaterialParentRow> Members;
};
namespace MaterialRegistryDetail
{
inline std::string Key(std::string Value)
{
    for (auto& C : Value)
        if (C >= 'A' && C <= 'Z')
            C = char(C - 'A' + 'a');
    return Value;
}
inline bool Ascii(const std::string& Value)
{
    for (unsigned char C : Value)
        if (C >= 128)
            return false;
    return true;
}
inline std::string Trim(const std::string& Value)
{
    const auto A = Value.find_first_not_of(" \t\r"), B = Value.find_last_not_of(" \t\r");
    return A == std::string::npos ? std::string() : Value.substr(A, B - A + 1);
}
inline bool Utf8(const std::string& Value)
{
    if (Value.size() > 65535)
        return false;
    std::vector<std::uint8_t> Bytes(4);
    const auto Count = Value.size() + 1;
    for (unsigned I = 0; I < 4; ++I)
        Bytes[I] = std::uint8_t(Count >> (I * 8));
    Bytes.insert(Bytes.end(), Value.begin(), Value.end());
    Bytes.push_back(0);
    FByteReader Reader(Bytes);
    std::string Out;
    return Reader.String(Out) && Out == Value;
}
inline bool Package(const std::string& Value)
{
    if (Value.size() < 4 || Value[0] != '/' || Value.back() == '/' || Value.find('/', 1) == std::string::npos ||
        !Utf8(Value))
        return false;
    for (unsigned char C : Value)
    {
        if (C == '/')
            continue;
        if (C < 0x21 || std::string("\\:*?\"<>|' ,.&!~@#()[]").find(char(C)) != std::string::npos)
            return false;
    }
    return Value.find("//") == std::string::npos;
}
inline bool Object(const std::string& Value, std::string& PackageName)
{
    const auto Dot = Value.find('.');
    if (Dot == std::string::npos || Dot + 1 == Value.size() || Value.find('.', Dot + 1) != std::string::npos)
        return false;
    PackageName = Value.substr(0, Dot);
    if (!Package(PackageName))
        return false;
    const auto Leaf = Value.substr(Dot + 1);
    if (!Utf8(Leaf))
        return false;
    for (unsigned char C : Leaf)
        if (C < 0x21 || C == '/' || std::string("\\:*?\"<>|' ,.&!~@#()[]").find(char(C)) != std::string::npos)
            return false;
    return true;
}
inline bool Quoted(const std::string& Source, std::string& Value)
{
    const auto Text = Trim(Source);
    if (Text.size() < 3 || Text.front() != '\"' || Text.substr(Text.size() - 2) != "\",")
        return false;
    Value = Text.substr(1, Text.size() - 3);
    return Value.find_first_of("\\\"\r\n") == std::string::npos && Utf8(Value);
}
inline bool Parent(const std::string& Value, FMaterialParentRow& Row)
{
    const auto Quote = Value.find('\'');
    if (Quote == std::string::npos || Value.back() != '\'')
        return false;
    Row.ParentClass = Value.substr(0, Quote);
    if (Row.ParentClass != "Material" && Row.ParentClass != "MaterialInstanceConstant")
        return false;
    Row.ParentObject = Value.substr(Quote + 1, Value.size() - Quote - 2);
    return Object(Row.ParentObject, Row.ParentPackage);
}
}

// Parses the observed AddedDependencyFlags retoc pretty stream incrementally.
// External/unselected ancestry is retained; selected unsupported roots refuse.
// Valid() proves format/census completeness only. The caller must independently
// bind the stream to its current selected game registry and tool identities.
class FMaterialParentRegistry
{
    struct FRecord
    {
        std::string Package, Object, Class, Parent;
        std::set<std::string> Fields;
        unsigned Parents = 0;
        bool BadIdentity = false;
    };
    enum class ETag
    {
        None,
        Key,
        Value,
        Close
    } Tag = ETag::None;
    FRecord Record;
    bool InRecord = false, InTags = false, ParentTag = false, LongLine = false, Finished = false, Version = false,
         HasCount = false;
    std::string Line, ErrorText;
    std::size_t Bytes = 0, Count = 0, Declared = 0, Strings = 0, MicCount = 0;
    std::map<std::string, FMaterialParentRow> Data;
    std::map<std::string, std::string> PackageSpellings;
    std::map<std::string, std::vector<std::string>> Children;
    bool Fail(const char* Why)
    {
        if (ErrorText.empty())
            ErrorText = Why;
        return false;
    }
    bool Store()
    {
        if (Record.Fields.count("asset_class") != 1 || Record.Class.empty())
            return Fail("material-registry-class-missing-or-invalid");
        const bool Mic = Record.Class == "MaterialInstanceConstant", Master = Record.Class == "Material";
        if (Mic)
            ++MicCount;
        if (Master && Record.Parents)
            return Fail("material-registry-root-has-parent");
        if (!Mic && !Master && Record.Parents == 0)
            return true;
        FMaterialParentRow Row;
        Row.Package = Record.Package;
        Row.Object = Record.Object;
        Row.Class = Record.Class;
        if (!Master)
        {
            if (Record.Parents != 1 || !MaterialRegistryDetail::Parent(Record.Parent, Row))
            {
                if (Mic || MaterialRegistryDetail::Key(MaterialRegistryDetail::Trim(Record.Parent))
                                   .compare(0, 8, "material") == 0)
                    return Fail("material-registry-parent-invalid");
                return true;
            }
        }
        std::string Package;
        if (Record.BadIdentity || !MaterialRegistryDetail::Package(Row.Package) ||
            !MaterialRegistryDetail::Object(Row.Object, Package) || Package != Row.Package)
            return Fail("material-registry-object-identity-invalid");
        const auto PackageKey = MaterialRegistryDetail::Key(Row.Package),
                   ObjectKey = MaterialRegistryDetail::Key(Row.Object);
        const auto Prior = PackageSpellings.find(PackageKey);
        if (Prior != PackageSpellings.end() && Prior->second != Row.Package)
            return Fail("material-registry-package-case-alias");
        if (Data.count(ObjectKey))
            return Fail("material-registry-object-duplicate-or-case-alias");
        const auto Size = Row.Package.size() + Row.Object.size() + Row.Class.size() + Row.ParentPackage.size() +
                          Row.ParentObject.size() + Row.ParentClass.size();
        if (Size > 32 * 1024 * 1024 - Strings)
            return Fail("material-registry-string-bound");
        Strings += Size;
        PackageSpellings.emplace(PackageKey, Row.Package);
        Data.emplace(ObjectKey, Row);
        if (!Master)
            Children[MaterialRegistryDetail::Key(Row.ParentObject)].push_back(ObjectKey);
        return true;
    }
    bool EndLine()
    {
        if (!Line.empty() && Line.back() == '\r')
            Line.pop_back();
        const auto Text = MaterialRegistryDetail::Trim(Line);
        if (LongLine)
        {
            if (Line.find("    object_path:") == 0 || Line.find("    package_name:") == 0 ||
                Line.find("    asset_class:") == 0)
                return Fail("material-registry-identity-line-bound");
            if (InRecord && InTags)
            {
                if (Tag != ETag::Value || ParentTag)
                    return Fail("material-registry-parent-line-bound");
                Tag = ETag::Close;
            }
        }
        else if (!InRecord)
        {
            const std::string Prefix = "Registry Version: ";
            if (Text.compare(0, Prefix.size(), Prefix) == 0)
            {
                if (Version || Text != Prefix + "AddedDependencyFlags")
                    return Fail("material-registry-version-unsupported");
                Version = true;
            }
            else if (Text.compare(0, 8, "Assets: ") == 0)
            {
                if (HasCount || Text.size() == 8)
                    return Fail("material-registry-count-invalid");
                HasCount = true;
                for (std::size_t I = 8; I < Text.size(); ++I)
                {
                    if (Text[I] < '0' || Text[I] > '9' || Declared > 1000000 / 10)
                        return Fail("material-registry-count-bound");
                    Declared = Declared * 10 + unsigned(Text[I] - '0');
                }
                if (!Declared || Declared > 1000000)
                    return Fail("material-registry-count-bound");
            }
            else if (Text == "AssetData {")
            {
                if (!Version || !HasCount)
                    return Fail("material-registry-header-incomplete");
                Record = FRecord();
                InRecord = true;
                InTags = false;
                Tag = ETag::None;
            }
        }
        else if (Text == "AssetData {")
            return Fail("material-registry-record-nested");
        else if (Line == "}")
        {
            if (InTags || Tag != ETag::None)
                return Fail("material-registry-tag-truncated");
            if (++Count > Declared || !Store())
                return Fail("material-registry-count-exceeded");
            InRecord = false;
        }
        else
        {
            bool Identity = false;
            for (const char* Name : {"object_path", "package_name", "asset_class"})
            {
                const std::string Prefix = std::string("    ") + Name + ": ";
                if (Line.compare(0, Prefix.size(), Prefix) != 0)
                    continue;
                Identity = true;
                if (!Record.Fields.insert(Name).second)
                    return Fail("material-registry-field-duplicate");
                std::string Value;
                if (!MaterialRegistryDetail::Quoted(Line.substr(Prefix.size()), Value))
                {
                    if (std::string(Name) == "asset_class")
                        return Fail("material-registry-class-missing-or-invalid");
                    Record.BadIdentity = true;
                }
                if (std::string(Name) == "object_path")
                    Record.Object = Value;
                else if (std::string(Name) == "package_name")
                    Record.Package = Value;
                else
                    Record.Class = Value;
            }
            if (!Identity)
            {
                if (Line == "    tags: [" || Line == "    legacy_tags: [")
                {
                    if (InTags)
                        return Fail("material-registry-tags-nested");
                    if (Record.Class.empty())
                        return Fail("material-registry-class-before-tags-required");
                    InTags = true;
                    Tag = ETag::None;
                }
                else if ((Line.compare(0, 10, "    tags: ") == 0 || Line.compare(0, 17, "    legacy_tags: ") == 0) &&
                         Line != "    tags: []," && Line != "    legacy_tags: [],")
                    return Fail("material-registry-tags-format-invalid");
                else if (InTags && Line == "    ],")
                {
                    if (Tag != ETag::None)
                        return Fail("material-registry-tag-truncated");
                    InTags = false;
                }
                else if (InTags && !Text.empty())
                {
                    if (Text == "(")
                    {
                        if (Tag != ETag::None)
                            return Fail("material-registry-tuple-nested");
                        Tag = ETag::Key;
                    }
                    else if (Tag == ETag::Key)
                    {
                        std::string Value;
                        if (!MaterialRegistryDetail::Quoted(Text, Value))
                            return Fail("material-registry-tag-key-invalid");
                        ParentTag = Value == "Parent";
                        Tag = ETag::Value;
                    }
                    else if (Tag == ETag::Value)
                    {
                        if (ParentTag)
                        {
                            if (!MaterialRegistryDetail::Quoted(Text, Record.Parent) || ++Record.Parents != 1)
                                return Fail("material-registry-parent-duplicate-or-invalid");
                        }
                        Tag = ETag::Close;
                    }
                    else if (Tag == ETag::Close)
                    {
                        if (Text != "),")
                            return Fail("material-registry-tag-close-invalid");
                        Tag = ETag::None;
                        ParentTag = false;
                    }
                    else
                        return Fail("material-registry-tag-entry-invalid");
                }
            }
        }
        Line.clear();
        LongLine = false;
        return ErrorText.empty();
    }

public:
    bool Feed(const std::uint8_t* Input, std::size_t Size)
    {
        if (!ErrorText.empty())
            return false;
        if (Finished)
            return Fail("material-registry-feed-after-finish");
        if (Size && !Input)
            return Fail("material-registry-null-input");
        if (Size > std::size_t(1u << 30) - Bytes)
            return Fail("material-registry-stream-bound");
        Bytes += Size;
        for (std::size_t I = 0; I < Size; ++I)
        {
            const auto C = Input[I];
            if (C == '\n')
            {
                if (!EndLine())
                    return false;
            }
            else if (Line.size() < 65600)
                Line.push_back(char(C));
            else
                LongLine = true;
        }
        return true;
    }
    bool Finish()
    {
        if (!ErrorText.empty())
            return false;
        if (Finished)
            return true;
        if ((!Line.empty() || LongLine) && !EndLine())
            return false;
        if (InRecord || InTags || !Version || !HasCount || Count != Declared)
            return Fail("material-registry-incomplete-census");
        Finished = true;
        for (auto& Pair : Children)
            std::sort(Pair.second.begin(), Pair.second.end());
        return true;
    }
    bool Valid() const
    {
        return Finished && ErrorText.empty();
    }
    const std::string& Error() const
    {
        return ErrorText;
    }
    std::size_t AssetCount() const
    {
        return Count;
    }
    std::size_t MaterialInstanceCount() const
    {
        return MicCount;
    }
    const std::map<std::string, FMaterialParentRow>& Rows() const
    {
        return Data;
    }
    bool Family(const std::string& Selection, FMaterialRegistryFamily& Out, std::string& Why) const
    {
        Out = FMaterialRegistryFamily();
        Why.clear();
        const auto FailQuery = [&](const char* Error)
        {
            Out = FMaterialRegistryFamily();
            Why = Error;
            return false;
        };
        if (!Valid())
            return FailQuery("material-family-registry-unqualified");
        const FMaterialParentRow* Row = nullptr;
        const auto Key = MaterialRegistryDetail::Key(Selection);
        for (const auto& Pair : Data)
            if (Pair.first == Key || MaterialRegistryDetail::Key(Pair.second.Package) == Key)
            {
                if (Row)
                    return FailQuery("material-family-selection-ambiguous");
                Row = &Pair.second;
            }
        if (!Row)
            return FailQuery("material-family-selection-missing");
        std::set<std::string> Ancestors;
        while (Row->Class != "Material")
        {
            if (Row->Class != "MaterialInstanceConstant")
                return FailQuery("material-family-class-unsupported");
            if (!MaterialRegistryDetail::Ascii(Row->Object) || !MaterialRegistryDetail::Ascii(Row->ParentObject))
                return FailQuery("material-family-unicode-identity-unqualified");
            if (!Ancestors.insert(MaterialRegistryDetail::Key(Row->Object)).second)
                return FailQuery("material-family-parent-cycle");
            if (Ancestors.size() > 256)
                return FailQuery("material-family-parent-depth");
            const auto Parent = Data.find(MaterialRegistryDetail::Key(Row->ParentObject));
            if (Parent == Data.end())
                return FailQuery(Row->ParentPackage.compare(0, 6, "/Game/") == 0
                                     ? "material-family-parent-source-missing"
                                     : "material-family-external-root-unsupported");
            if (Parent->second.Class != Row->ParentClass)
                return FailQuery("material-family-parent-class-conflict");
            Row = &Parent->second;
        }
        if (Row->Package.compare(0, 6, "/Game/") != 0)
            return FailQuery("material-family-external-root-unsupported");
        if (!MaterialRegistryDetail::Ascii(Row->Object))
            return FailQuery("material-family-unicode-identity-unqualified");
        Out.Master = *Row;
        std::vector<std::pair<std::string, std::size_t>> Pending{{MaterialRegistryDetail::Key(Row->Object), 0}};
        std::set<std::string> Seen;
        while (!Pending.empty())
        {
            const auto Next = Pending.back();
            Pending.pop_back();
            const auto& Current = Next.first;
            if (Next.second > 256)
                return FailQuery("material-family-parent-depth");
            if (!Seen.insert(Current).second)
                continue;
            if (Seen.size() > 4096)
                return FailQuery("material-family-member-bound");
            const auto& Member = Data.at(Current);
            if (Member.Class != "Material" && Member.Class != "MaterialInstanceConstant")
                return FailQuery("material-family-class-unsupported");
            if (!MaterialRegistryDetail::Ascii(Member.Object))
                return FailQuery("material-family-unicode-identity-unqualified");
            Out.Members.push_back(Member);
            const auto Found = Children.find(Current);
            if (Found == Children.end())
                continue;
            for (const auto& Child : Found->second)
            {
                if (Data.at(Child).ParentClass != Member.Class)
                    return FailQuery("material-family-parent-class-conflict");
                Pending.emplace_back(Child, Next.second + 1);
            }
        }
        std::sort(Out.Members.begin(), Out.Members.end(),
                  [](const FMaterialParentRow& A, const FMaterialParentRow& B) { return A.Object < B.Object; });
        return true;
    }
};
}
}

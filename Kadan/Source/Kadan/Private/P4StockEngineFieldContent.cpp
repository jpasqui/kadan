#include "P4StockEngineFieldContent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "P4MapKitCore/postprocess_cook.h"
#include "P4MapKitCore/cooked_export_rebuild.h"
#include "P4TextureBuild.h"

using P4MapKit::FDecodedValue;
namespace
{
FDecodedValue Number(uint32 Value)
{
    return FDecodedValue::Atom(FDecodedValue::EKind::Number, std::to_string(Value));
}
bool Unsigned(const FDecodedValue* Value, uint32& Out)
{
    return Value && Value->Kind == FDecodedValue::EKind::Number &&
        LexTryParseString(Out, UTF8_TO_TCHAR(Value->Text.c_str())) && std::to_string(Out) == Value->Text;
}
} //namespace

bool P4StoreDecodedStockEngineField(UObject* Object, const FString& Name, const FDecodedValue& Value, FString& Error)
{
    FP4StockEngineField Field;
    Field.Name = FName(*Name);
    bool Ok = false;
    if (Field.Name == TEXT("C4MaterialMeshType"))
        Ok = Value.Kind == FDecodedValue::EKind::Number &&
            LexTryParseString(Field.Integer, UTF8_TO_TCHAR(Value.Text.c_str())) &&
            std::to_string(Field.Integer) == Value.Text;
    else if (Field.Name == TEXT("C4Stencil"))
    {
        Ok = Value.Kind == FDecodedValue::EKind::String;
        Field.Choice = FName(UTF8_TO_TCHAR(Value.Text.c_str()));
    }
    else if (Field.Name == TEXT("C4CollisionMaterial"))
    {
        Ok = Value.Kind == FDecodedValue::EKind::Null;
        if (Value.Kind == FDecodedValue::EKind::String)
        {
            Field.Material = LoadObject<UMaterialInterface>(nullptr, UTF8_TO_TCHAR(Value.Text.c_str()));
            Ok = Field.Material != nullptr;
        }
    }
    else if (Field.Name == TEXT("C4TerrainTagContainer"))
    {
        const FDecodedValue* Tags = Value.Field("TerrainTags");
        Ok = Value.Kind == FDecodedValue::EKind::Object && Value.Fields.size() == 1 && Tags &&
            Tags->Kind == FDecodedValue::EKind::Array && Tags->Items.size() <= 256;
        if (Ok)
            for (const FDecodedValue& Tag : Tags->Items)
            {
                uint32 Number = 0;
                if (Tag.Kind != FDecodedValue::EKind::Object || Tag.Fields.size() != 1 ||
                    !Unsigned(Tag.Field("TerrainTag"), Number))
                {
                    Ok = false;
                    break;
                }
                Field.Tags.Add(Number);
            }
    }
    if (!Ok)
    {
        Error = TEXT("stock-engine-field-source-type-or-value-invalid: ") + Name + TEXT(" = ") +
            UTF8_TO_TCHAR(P4MapKit::DecodedJson(Value).c_str());
        return false;
    }
    return P4SetStockEngineField(Object, Field, Error);
}

FDecodedValue P4DecodeStockEngineField(const FP4StockEngineField& Field)
{
    if (Field.Name == TEXT("C4MaterialMeshType"))
        return FDecodedValue::Atom(FDecodedValue::EKind::Number, std::to_string(Field.Integer));
    if (Field.Name == TEXT("C4Stencil"))
        return FDecodedValue::Atom(FDecodedValue::EKind::String, TCHAR_TO_UTF8(*Field.Choice.ToString()));
    if (Field.Name == TEXT("C4CollisionMaterial"))
        return FDecodedValue::Atom(Field.Material ? FDecodedValue::EKind::String : FDecodedValue::EKind::Null,
            Field.Material ? TCHAR_TO_UTF8(*Field.Material->GetPathName()) : "");
    FDecodedValue Container;
    Container.Kind = FDecodedValue::EKind::Object;
    FDecodedValue Tags;
    Tags.Kind = FDecodedValue::EKind::Array;
    for (uint32 Tag : Field.Tags)
    {
        FDecodedValue Row;
        Row.Kind = FDecodedValue::EKind::Object;
        Row.Fields.emplace_back("TerrainTag", Number(Tag));
        Tags.Items.push_back(std::move(Row));
    }
    Container.Fields.emplace_back("TerrainTags", std::move(Tags));
    return Container;
}

namespace
{
using Bytes = std::vector<uint8>;
using Tag = P4MapKit::PostProcessCook::FSourceTag;
using namespace P4MapKit::PostProcessCook::Detail;
bool WriteTag(Bytes& Out, std::vector<std::string>& Names, const char* Name, const char* Type, const Bytes& Payload,
    const std::vector<std::string>& Detail = {})
{
    Tag Metadata;
    Metadata.Name = Name;
    Metadata.Type = Type;
    Metadata.Detail = Detail;
    return P4MapKit::PostProcessCook::Detail::Tag(Out, Names, Metadata, {}, Payload);
}
std::int32_t MaterialImport(const UMaterialInterface* Material, const P4MapKit::FPackageInventory& Native,
    std::vector<P4MapKit::FPackageImport>& Imports)
{
    if (!Material)
        return 0;
    const FString Path = Material->GetPathName();
    std::string Reference;
    for (std::size_t At = 0; At < Native.Exports.size(); ++At)
        if (Native.Reference(int32(At + 1), Reference) &&
            Path.Equals(UTF8_TO_TCHAR(Reference.c_str()), ESearchCase::IgnoreCase))
            return int32(At + 1);
    // Appending preserves every existing import and export index.
    const std::string Package = TCHAR_TO_UTF8(*Material->GetOutermost()->GetName());
    int32 Outer = 0;
    for (std::size_t At = 0; At < Imports.size(); ++At)
    {
        const auto& Item = Imports[At];
        if (!Item.Outer && Item.Name == Package && Item.Class == "Package")
            Outer = -int32(At + 1);
    }
    if (!Outer)
    {
        Imports.push_back({"/Script/CoreUObject", "Package", Package, 0});
        Outer = -int32(Imports.size());
    }
    const std::string Name = TCHAR_TO_UTF8(*Material->GetName());
    const std::string Class = TCHAR_TO_UTF8(*Material->GetClass()->GetName());
    const std::string ClassPackage = TCHAR_TO_UTF8(*Material->GetClass()->GetOutermost()->GetName());
    for (std::size_t At = 0; At < Imports.size(); ++At)
        if (Imports[At].Outer == Outer && Imports[At].Name == Name && Imports[At].Class == Class &&
            Imports[At].ClassPackage == ClassPackage)
            return -int32(At + 1);
    Imports.push_back({ClassPackage, Class, Name, Outer});
    return -int32(Imports.size());
}
bool Payload(const FP4StockEngineField& Field, const P4MapKit::FPackageInventory& Native,
    std::vector<P4MapKit::FPackageImport>& Imports, std::vector<std::string>& Names, Bytes& Out)
{
    Bytes Value;
    if (Field.Name == TEXT("C4MaterialMeshType"))
    {
        U(Value, uint32(Field.Integer));
        return WriteTag(Out, Names, "C4MaterialMeshType", "IntProperty", Value);
    }
    if (Field.Name == TEXT("C4Stencil"))
    {
        if (!Name(Value, Names, TCHAR_TO_UTF8(*Field.Choice.ToString())))
            return false;
        return WriteTag(Out, Names, "C4Stencil", "EnumProperty", Value, {"EC4Stencil"});
    }
    if (Field.Name == TEXT("C4CollisionMaterial"))
    {
        U(Value, uint32(MaterialImport(Field.Material, Native, Imports)));
        return WriteTag(Out, Names, "C4CollisionMaterial", "ObjectProperty", Value);
    }
    if (Field.Name != TEXT("C4TerrainTagContainer") || Field.Tags.Num() > 256)
        return false;
    Bytes Rows;
    for (uint32 Number : Field.Tags)
    {
        Bytes One;
        U(One, Number);
        if (!WriteTag(Rows, Names, "TerrainTag", "UInt32Property", One) || !Name(Rows, Names, "None"))
            return false;
    }
    Bytes Array;
    U(Array, Field.Tags.Num());
    if (!WriteTag(Array, Names, "TerrainTags", "StructProperty", Rows, {"C4TerrainTag"}) ||
        !WriteTag(Value, Names, "TerrainTags", "ArrayProperty", Array, {"StructProperty"}) ||
        !Name(Value, Names, "None"))
        return false;
    return WriteTag(Out, Names, "C4TerrainTagContainer", "StructProperty", Value, {"C4TerrainTagContainer"});
}
bool AppendImports(
    Bytes& Header, const Bytes& Body, const std::vector<P4MapKit::FPackageImport>& Imports, FString& Error)
{
    using namespace P4MapKit::Export::PackageRewriteDetail;
    const auto Native = P4MapKit::ReadPackage(Header, Body);
    if (!Native.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(Native.Error.Code.c_str());
        return false;
    }
    if (Imports.size() == Native.Imports.size())
        return true;
    P4MapKit::FByteReader Summary(Header, 20);
    std::size_t Count = 0, Declared = 0;
    std::string Text;
    if (!Summary.Count(Count, 1000) || !Summary.Skip(Count * 20))
        return false;
    const auto HeaderSizeAt = Summary.Tell();
    if (!Summary.Count(Declared, Header.size()) || !Summary.String(Text))
        return false;
    const auto FlagsAt = Summary.Tell();
    if (!Summary.Skip(72) || !Summary.Count(Count, 100) || !Summary.Skip(Count * 8))
        return false;
    for (int At = 0; At < 2; ++At)
        if (!Summary.Skip(10) || !Summary.String(Text))
            return false;
    if (!Summary.Skip(12) || !Summary.Count(Count, 100))
        return false;
    for (std::size_t At = 0; At < Count; ++At)
        if (!Summary.String(Text))
            return false;
    const auto BulkAt = Summary.Tell() + 4;
    std::uint64_t ExportsAt = 0;
    if (!Scalar(Header, FlagsAt + 24, ExportsAt))
        return false;
    const auto OldSize = Header.size();
    auto Names = Native.Names;
    Bytes Table;
    for (const auto& Item : Imports)
    {
        if (!Name(Table, Names, Item.ClassPackage) || !Name(Table, Names, Item.Class))
            return false;
        U(Table, uint32(Item.Outer));
        if (!Name(Table, Names, Item.Name))
            return false;
    }
    if (Names != Native.Names || Table.size() != Imports.size() * 28)
        return false;
    Header.insert(Header.end(), Table.begin(), Table.end());
    Put(Header, FlagsAt + 28, Imports.size());
    Put(Header, FlagsAt + 32, OldSize);
    Put(Header, HeaderSizeAt, Header.size());
    Put(Header, BulkAt, Header.size() + Body.size() - 4, 8);
    for (std::size_t At = 0; At < Native.Exports.size(); ++At)
        Put(Header, std::size_t(ExportsAt) + At * 104 + 36, Header.size() + Native.Exports[At].Offset, 8);
    const auto Checked = P4MapKit::ReadPackage(Header, Body);
    if (!Checked.Error.Code.empty() || Checked.Imports.size() != Imports.size())
        return false;
    Error.Reset();
    return true;
}
} //namespace

bool P4AppendCookedImports(std::vector<uint8>& Header, const std::vector<uint8>& Body,
    const std::vector<P4MapKit::FPackageImport>& Imports, FString& Error)
{
    Error.Reset();
    if (AppendImports(Header, Body, Imports, Error))
        return true;
    if (Error.IsEmpty())
        Error = TEXT("cooked-import-table-not-appended");
    return false;
}

namespace
{
TArray<FP4StockEngineField> CookFields(UObject* Object)
{
    if (!IsValid(Object) || Object->HasAnyFlags(RF_Transient) || Object->IsEditorOnly())
        return {};
    TArray<FP4StockEngineField> Fields = P4GetStockEngineFields(Object);
    if (!Fields.Num())
        return Fields;
    const UObject* Archetype = Object->GetArchetype();
    if (Archetype && Archetype != Object && Object->GetClass()->IsChildOf(Archetype->GetClass()))
    {
        const auto Defaults = P4GetStockEngineFields(Archetype);
        Fields.RemoveAll(
            [&](const FP4StockEngineField& Field)
            {
                for (const auto& Default : Defaults)
                    if (Default.Name == Field.Name &&
                        P4MapKit::DecodedJson(P4DecodeStockEngineField(Default)) ==
                            P4MapKit::DecodedJson(P4DecodeStockEngineField(Field)))
                        return true;
                return false;
            });
    }
    return Fields;
}
} //namespace

bool P4PackageHasStockEngineFields(UPackage* Package)
{
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    for (UObject* Object : Objects)
        if (CookFields(Object).Num())
            return true;
    return false;
}

bool P4TranslateStockEngineFieldCook(
    UPackage* Package, std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error)
{
    TMap<FString, TArray<FP4StockEngineField>> Fields;
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    for (UObject* Object : Objects)
    {
        TArray<FP4StockEngineField> Stored = CookFields(Object);
        if (!Stored.Num())
            continue;
        FString Path = Object->GetPathName(Package);
        Path.ReplaceInline(TEXT(":"), TEXT("."));
        Fields.Add(Path, MoveTemp(Stored));
    }
    if (!Fields.Num())
    {
        Error.Reset();
        return true;
    }
    const auto Native = P4MapKit::ReadPackage(Header, Body);
    if (!Native.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(Native.Error.Code.c_str());
        return false;
    }
    if (!P4ValidateStockEngineCookRelocation(Header, Body, Error))
        return false;
    auto Names = Native.Names;
    auto Imports = Native.Imports;
    std::map<std::size_t, Bytes> Replacements;
    TSet<FString> Written;
    for (std::size_t At = 0; At < Native.Exports.size(); ++At)
    {
        const auto& Export = Native.Exports[At];
        std::string Path;
        if (!Native.Reference(int32(At + 1), Path))
            return false;
        const auto* Stored = Fields.Find(UTF8_TO_TCHAR(Path.c_str()));
        if (!Stored)
            continue;
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(
            Body, Native.Names, Export.Offset, Export.Offset + Export.Size);
        if (!Tags.Error.Code.empty())
            return false;
        Bytes Rebuilt(Body.begin() + Export.Offset, Body.begin() + Tags.End - 8);
        for (const auto& Field : *Stored)
        {
            for (const auto& Tag : Tags.Properties)
                if (FName(UTF8_TO_TCHAR(Tag.Name.c_str())) == Field.Name)
                {
                    Error = TEXT("stock-engine-field-cook-would-shadow-native-field: ") + Field.Name.ToString();
                    return false;
                }
            if (!Payload(Field, Native, Imports, Names, Rebuilt))
            {
                Error = TEXT("stock-engine-field-cook-value-invalid");
                return false;
            }
        }
        if (!Name(Rebuilt, Names, "None"))
            return false;
        Rebuilt.insert(Rebuilt.end(), Body.begin() + Tags.End, Body.begin() + Export.Offset + Export.Size);
        Replacements.emplace(At, std::move(Rebuilt));
        Written.Add(UTF8_TO_TCHAR(Path.c_str()));
    }
    if (Written.Num() != Fields.Num())
    {
        Error = TEXT("stock-engine-field-cook-object-not-serialized");
        return false;
    }
    Bytes Unused;
    // The names of the imports added here. The package's own imports have theirs already (as a name and a number,
    // which the reader joins).
    for (std::size_t N = Native.Imports.size(); N < Imports.size(); ++N)
        if (!Name(Unused, Names, Imports[N].Name) || !Name(Unused, Names, Imports[N].Class) ||
            !Name(Unused, Names, Imports[N].ClassPackage))
            return false;
    P4MapKit::Export::RewrittenPackage Candidate;
    std::string Why;
    if (!P4MapKit::Export::RebuildCookedExports(Header, Body, Names, Replacements, Candidate, Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    if (!AppendImports(Candidate.Header, Candidate.Body, Imports, Error))
    {
        if (Error.IsEmpty())
            Error = TEXT("stock-engine-field-cook-import-table-invalid");
        return false;
    }
    if (!P4RelocatePackageTextureImages(Header, Body, Candidate.Header, Candidate.Body, Error))
        return false;
    // Decode the actual output again: a stored field counts only when the emitted typed value agrees.
    const auto Checked = P4MapKit::ReadPackage(Candidate.Header, Candidate.Body);
    for (const auto& Pair : Replacements)
    {
        const auto& Export = Checked.Exports[Pair.first];
        std::string Path;
        Checked.Reference(int32(Pair.first + 1), Path);
        P4MapKit::FPropertyDecoder Decoder(Checked, Candidate.Body);
        FDecodedValue Values;
        std::size_t Position = Export.Offset;
        if (!Decoder.Properties(Position, Export.Offset + Export.Size, Values, 0, Export.Name))
        {
            Error = FString(TEXT("stock-engine-field-cook-readback-failed: ")) +
                UTF8_TO_TCHAR(Decoder.Error.Code.c_str());
            return false;
        }
        for (const auto& Field : Fields[UTF8_TO_TCHAR(Path.c_str())])
        {
            const FDecodedValue* Value = Values.Field(TCHAR_TO_UTF8(*Field.Name.ToString()));
            if (!Value || P4MapKit::DecodedJson(*Value) != P4MapKit::DecodedJson(P4DecodeStockEngineField(Field)))
            {
                Error = TEXT("stock-engine-field-cook-value-differs: ") + Field.Name.ToString();
                return false;
            }
        }
    }
    Header = std::move(Candidate.Header);
    Body = std::move(Candidate.Body);
    Error.Reset();
    return true;
}

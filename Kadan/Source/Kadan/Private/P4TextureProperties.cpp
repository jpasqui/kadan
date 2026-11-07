#include "P4TextureProperties.h"
#include "Engine/Texture.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "P4NativeValueStaging.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
FString Native(const std::string& Text)
{
    return UTF8_TO_TCHAR(Text.c_str());
}
bool Refuse(FString& Error, const FString& Field, const TCHAR* Reason)
{
    Error = Field + TEXT(": ") + Reason;
    return false;
}
bool TextureClass(UClass* Schema)
{
    if (!Schema || !Schema->IsChildOf(UTexture::StaticClass()))
        return false;
    const FString Path = Schema->GetPathName();
    return Path == TEXT("/Script/Engine.Texture2D") || Path == TEXT("/Script/Engine.LightMapTexture2D") ||
        Path == TEXT("/Script/Engine.ShadowMapTexture2D") || Path == TEXT("/Script/Engine.TextureCube") ||
        Path == TEXT("/Script/Engine.Texture2DArray") || Path == TEXT("/Script/Engine.VolumeTexture");
}
// The game's engine adds fifteen texture groups at the values 34 to 48, which is where the stock
// engine has its fifteen project groups. The game's own project groups follow at 49 to 63 and have
// no place in the stock list.
const TCHAR* const GameTextureGroups[15] = {TEXT("Characters_ColorMap"), TEXT("Characters_NormalMap"),
    TEXT("Characters_MaskMap"), TEXT("Carrot_ColorMap"), TEXT("Carrot_NormalMap"), TEXT("Carrot_MaskMap"),
    TEXT("Terrain_ColorMap"), TEXT("Terrain_NormalMap"), TEXT("Terrain_MaskMap"), TEXT("Objects_ColorMap"),
    TEXT("Objects_NormalMap"), TEXT("Objects_MaskMap"), TEXT("UI_2D"), TEXT("UI_3D"), TEXT("Debug_Collision")};
// Gives a texture in one of those groups the project group of the same value and keeps the game's
// name in the document. False, with the reason, for a group the stock list cannot hold.
bool EditorTextureGroup(P4MapKit::FDecodedValue& Value, FP4TexturePropertyDocument& Document, FString& Issue)
{
    if (Value.Kind != P4MapKit::FDecodedValue::EKind::String)
        return true;
    const FString Name = Native(Value.Text);
    if (Name.StartsWith(TEXT("TEXTUREGROUP_Project"), ESearchCase::CaseSensitive))
    {
        Issue = TEXT("LODGroup: the game's ") + Name + TEXT(" has no place in the stock engine's texture groups");
        return false;
    }
    for (int32 Index = 0; Index < 15; ++Index)
        if (Name.Equals(FString(TEXT("TEXTUREGROUP_")) + GameTextureGroups[Index], ESearchCase::CaseSensitive))
        {
            Document.GameTextureGroup = Name;
            Value.Text = TCHAR_TO_UTF8(*FString::Printf(TEXT("TEXTUREGROUP_Project%02d"), Index + 1));
        }
    return true;
}
bool EnvelopeSchema(FProperty* Field, const P4MapKit::FPropertyEnvelope& Tag, FString& Error)
{
    const FString Name = Native(Tag.Name);
    if (!Field || Field->HasAnyPropertyFlags(CPF_Transient | CPF_SkipSerialization) ||
        Tag.ArrayIndex >= std::size_t(Field->ArrayDim))
        return Refuse(Error, Name, TEXT("missing persistent native property or invalid array index"));
    if (Native(Tag.Type) != Field->GetClass()->GetName())
        return Refuse(Error, Name, TEXT("original tag type differs from installed reflected property"));
    if (auto Struct = CastField<FStructProperty>(Field))
    {
        if (Tag.Detail.size() != 1 || Native(Tag.Detail[0]) != Struct->Struct->GetName())
            return Refuse(Error, Name, TEXT("original struct type differs from installed native schema"));
    }
    if (auto Byte = CastField<FByteProperty>(Field))
    {
        const FString EnumName = Byte->Enum ? Byte->Enum->GetName() : TEXT("None");
        if (Tag.Detail.size() != 1 || Native(Tag.Detail[0]) != EnumName)
            return Refuse(Error, Name, TEXT("original byte enum differs from installed native schema"));
    }
    if (auto Enum = CastField<FEnumProperty>(Field))
        if (Tag.Detail.size() != 1 || !Enum->GetEnum() || Native(Tag.Detail[0]) != Enum->GetEnum()->GetName())
            return Refuse(Error, Name, TEXT("original enum differs from installed native schema"));
    if (auto Array = CastField<FArrayProperty>(Field))
        if (Tag.Detail.size() != 1 || Native(Tag.Detail[0]) != Array->Inner->GetClass()->GetName())
            return Refuse(Error, Name, TEXT("original array element differs from installed native schema"));
    if (auto Map = CastField<FMapProperty>(Field))
        if (Tag.Detail.size() != 2 || Native(Tag.Detail[0]) != Map->KeyProp->GetClass()->GetName() ||
            Native(Tag.Detail[1]) != Map->ValueProp->GetClass()->GetName())
            return Refuse(Error, Name, TEXT("original map element differs from installed native schema"));
    return true;
}
bool SourceValue(const P4MapKit::FPropertyEnvelope& Tag, const std::vector<std::uint8_t>& Body,
    P4MapKit::FPropertyDecoder& Decoder, const std::string& Class, P4MapKit::FDecodedValue& Value, FString& Error)
{
    // Engine FGuid archive is four uint32 words; its reflected NoExportTypes
    // schema uses int32 A/B/C/D. Preserve bits through signed field tokens.
    // No other native struct can take this adapter by its byte size alone.
    if (Tag.Type == "StructProperty" && Tag.Detail.size() == 1 && Tag.Detail[0] == "Guid")
    {
        if (Tag.HasGuid || Tag.Size != 16 || Tag.PayloadOffset > Body.size() ||
            Tag.Size > Body.size() - Tag.PayloadOffset)
            return Refuse(Error, Native(Tag.Name), TEXT("invalid Guid source archive"));
        P4MapKit::FByteReader Reader(Body, Tag.PayloadOffset, Tag.PayloadOffset + Tag.Size);
        for (const TCHAR* Name : {TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D")})
        {
            std::uint32_t Word = 0;
            if (!Reader.U32(Word))
                return Refuse(Error, Native(Tag.Name), TEXT("truncated Guid source archive"));
            const std::int64_t Signed = Word <= INT32_MAX ? std::int64_t(Word)
                                                          : std::int64_t(Word) - INT64_C(4294967296);
            Value.Fields.emplace_back(TCHAR_TO_UTF8(Name),
                P4MapKit::FDecodedValue::Atom(P4MapKit::FDecodedValue::EKind::Number, std::to_string(Signed)));
        }
        return true;
    }
    if (Decoder.Property(Tag, Value, 0, Class))
        return true;
    Error = Native(Tag.Name) + TEXT(": ") + Native(Decoder.Error.Code);
    return false;
}
void PreserveSourceReferences(const P4MapKit::FPropertyEnvelope& Tag, const P4MapKit::FDecodedValue& Value,
    const FString& Label, FP4TexturePropertyDocument& Document)
{
    using V = P4MapKit::FDecodedValue;
    const auto Add = [&](const std::string& Type, const V& Item, const FString& Field)
    {
        if ((Type != "ObjectProperty" && Type != "SoftObjectProperty") || Item.Kind != V::EKind::String ||
            Item.Text.empty())
            return;
        FP4TexturePropertyDocument::FDependency Dependency;
        Dependency.Field = Field;
        Dependency.Path = Native(Item.Text);
        Dependency.Soft = Type == "SoftObjectProperty";
        // Original tagged archives omit the property's declared object class.
        // Do not borrow an incompatible target property to qualify it.
        Document.Dependencies.Add(MoveTemp(Dependency));
    };
    Add(Tag.Type, Value, Label);
    if (Tag.Type == "ArrayProperty" && Tag.Detail.size() == 1 && Value.Kind == V::EKind::Array)
        for (std::size_t Index = 0; Index < Value.Items.size(); ++Index)
            Add(Tag.Detail[0], Value.Items[Index], Label + FString::Printf(TEXT("[%llu]"), uint64(Index)));
    if (Tag.Type == "MapProperty" && Tag.Detail.size() == 2)
    {
        const V* Pairs = Value.Kind == V::EKind::Object ? Value.Field("__native_map_pairs") : &Value;
        if (Pairs && Pairs->Kind == V::EKind::Array)
            for (const auto& Pair : Pairs->Items)
                if (Pair.Kind == V::EKind::Array && Pair.Items.size() == 2)
                {
                    Add(Tag.Detail[0], Pair.Items[0], Label + TEXT(".Key"));
                    Add(Tag.Detail[1], Pair.Items[1], Label + TEXT(".Value"));
                }
    }
}
bool Dependencies(FProperty* Property, const P4MapKit::FDecodedValue& Value, const FString& Label,
    FP4TexturePropertyDocument& Document, FString& Error, int32 Depth = 0)
{
    using V = P4MapKit::FDecodedValue;
    if (!Property || Depth >= 24)
        return Refuse(Error, Label, TEXT("incomplete dependency schema"));
    UEnum* Enum = nullptr;
    if (auto EnumProperty = CastField<FEnumProperty>(Property))
        Enum = EnumProperty->GetEnum();
    else if (auto Byte = CastField<FByteProperty>(Property))
        Enum = Byte->Enum;
    if (Enum && (Value.Kind != V::EKind::String || Enum->GetValueByNameString(Native(Value.Text)) == INDEX_NONE))
    {
        Error = Label + TEXT(": original enum value ") + Native(Value.Text) + TEXT(" unavailable in ") +
            Enum->GetPathName();
        return false;
    }
    if (auto Object = CastField<FObjectPropertyBase>(Property))
    {
        // A serialized hard-object index of zero is an explicit null value,
        // including inside arrays. It has no dependency to resolve.
        if (Value.Kind == V::EKind::Null && CastField<FObjectProperty>(Property) && Object->PropertyClass)
            return true;
        if (Value.Kind != V::EKind::String || !Object->PropertyClass)
            return Refuse(Error, Label, TEXT("unqualified native object reference"));
        if (!Value.Text.empty())
        {
            FP4TexturePropertyDocument::FDependency Item;
            Item.Field = Label;
            Item.Path = Native(Value.Text);
            Item.ExpectedClass = Object->PropertyClass->GetPathName();
            Item.Soft = CastField<FSoftObjectProperty>(Property) != nullptr;
            Document.Dependencies.Add(MoveTemp(Item));
        }
    }
    else if (auto Struct = CastField<FStructProperty>(Property))
    {
        // A soft path is written as its text. It names an asset without loading it.
        const FString StructPath = Struct->Struct->GetPathName();
        if (Value.Kind == V::EKind::String &&
            (StructPath == TEXT("/Script/CoreUObject.SoftObjectPath") ||
                StructPath == TEXT("/Script/CoreUObject.SoftClassPath")))
        {
            if (!Value.Text.empty())
            {
                FP4TexturePropertyDocument::FDependency Item;
                Item.Field = Label;
                Item.Path = Native(Value.Text);
                Item.Soft = true;
                Document.Dependencies.Add(MoveTemp(Item));
            }
            return true;
        }
        if (Value.Kind != V::EKind::Object)
            return Refuse(Error, Label, TEXT("unqualified native struct"));
        for (const auto& Child : Value.Fields)
        {
            const FString Name = Native(Child.first);
            if (!Dependencies(Struct->Struct->FindPropertyByName(FName(*Name)), Child.second, Label + TEXT(".") + Name,
                    Document, Error, Depth + 1))
                return false;
        }
    }
    else if (auto Array = CastField<FArrayProperty>(Property))
    {
        if (Value.Kind != V::EKind::Array)
            return Refuse(Error, Label, TEXT("unqualified native array"));
        for (std::size_t Index = 0; Index < Value.Items.size(); ++Index)
            if (!Dependencies(Array->Inner, Value.Items[Index], Label + FString::Printf(TEXT("[%llu]"), uint64(Index)),
                    Document, Error, Depth + 1))
                return false;
    }
    else if (auto Map = CastField<FMapProperty>(Property))
    {
        const V* Pairs = Value.Kind == V::EKind::Object ? Value.Field("__native_map_pairs") : &Value;
        if (!Pairs || Pairs->Kind != V::EKind::Array)
            return Refuse(Error, Label, TEXT("unqualified native map"));
        for (const auto& Pair : Pairs->Items)
            if (Pair.Kind != V::EKind::Array || Pair.Items.size() != 2 ||
                !Dependencies(Map->KeyProp, Pair.Items[0], Label + TEXT(".Key"), Document, Error, Depth + 1) ||
                !Dependencies(Map->ValueProp, Pair.Items[1], Label + TEXT(".Value"), Document, Error, Depth + 1))
                return false;
    }
    else if (CastField<FSetProperty>(Property) || CastField<FInterfaceProperty>(Property) ||
        CastField<FDelegateProperty>(Property) || CastField<FMulticastDelegateProperty>(Property))
        return Refuse(Error, Label, TEXT("native dependency/property kind remains unqualified"));
    return true;
}
TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const auto& Value : Values)
        Result.Add(MakeShared<FJsonValueString>(Value));
    return Result;
}
} //namespace

bool P4InspectTextureProperties(const P4MapKit::FPackageInventory& Package, const P4MapKit::FPackageExport& Export,
    const P4MapKit::FPropertyInventory& Properties, const std::vector<std::uint8_t>& Body, UClass* Schema,
    FP4TexturePropertyDocument& Document, FString& Error)
{
    Document = FP4TexturePropertyDocument();
    Error.Empty();
    if (!TextureClass(Schema))
        return Refuse(Error, TEXT("Texture"),
            TEXT("editor thread, supported complete schema and valid source inventories required"));
    return P4InspectObjectProperties(Package, Export, Properties, Body, Schema, TEXT("Texture"), TSet<FString>(),
        Document, Error,
        [](const FString& Key, P4MapKit::FDecodedValue& Value, FP4TexturePropertyDocument& Pending, FString& Issue)
        {
            return !Key.Equals(TEXT("LODGroup"), ESearchCase::CaseSensitive) ||
                EditorTextureGroup(Value, Pending, Issue);
        });
}

bool P4InspectObjectProperties(const P4MapKit::FPackageInventory& Package, const P4MapKit::FPackageExport& Export,
    const P4MapKit::FPropertyInventory& Properties, const std::vector<std::uint8_t>& Body, UClass* Schema,
    const TCHAR* Label, const TSet<FString>& Skip, FP4TexturePropertyDocument& Document, FString& Error,
    const TFunction<bool(const FString&, P4MapKit::FDecodedValue&, FP4TexturePropertyDocument&, FString&)>& Adapt,
    bool AbsentAsNotHeld)
{
    Document = FP4TexturePropertyDocument();
    Error.Empty();
    if (!IsInGameThread() || !Schema || !Package.Error.Code.empty() || !Properties.Error.Code.empty())
        return Refuse(
            Error, Label, TEXT("editor thread, supported complete schema and valid source inventories required"));
    std::string Class;
    // A class that is itself in the file (a level's script class) is named below the package.
    if (!Package.Reference(Export.Class, Class) ||
        (Export.Class > 0 ? Native(Package.Package) + TEXT(".") + Native(Class) : Native(Class)) !=
            Schema->GetPathName())
        return Refuse(Error, Label, TEXT("original class identity differs from installed schema"));
    if (Export.Offset > Body.size() || Export.Size > Body.size() - Export.Offset ||
        Properties.End > Export.Offset + Export.Size || Properties.End < Export.Offset)
        return Refuse(Error, Label, TEXT("property inventory outside source export"));
    const auto Actual = P4MapKit::ReadPropertyEnvelopes(
        Body, Package.Names, Export.Offset, Export.Offset + Export.Size);
    if (!Actual.Error.Code.empty() || Actual.End != Properties.End ||
        Actual.Properties.size() != Properties.Properties.size())
        return Refuse(Error, Label, TEXT("property inventory does not match fresh original source readback"));
    for (std::size_t Index = 0; Index < Actual.Properties.size(); ++Index)
    {
        const auto& A = Actual.Properties[Index];
        const auto& B = Properties.Properties[Index];
        if (A.Name != B.Name || A.Type != B.Type || A.Detail != B.Detail || A.TagOffset != B.TagOffset ||
            A.PayloadOffset != B.PayloadOffset || A.Size != B.Size || A.ArrayIndex != B.ArrayIndex ||
            A.BoolValue != B.BoolValue || A.HasGuid != B.HasGuid || A.StructGuid != B.StructGuid ||
            A.PropertyGuid != B.PropertyGuid)
            return Refuse(Error, Label, TEXT("property inventory was replaced or altered"));
    }
    FP4TexturePropertyDocument Pending;
    Pending.ClassPath = Schema->GetPathName();
    Pending.Package = Native(Package.Package);
    P4MapKit::FDecodedValue Values;
    P4MapKit::FPropertyDecoder Decoder(Package, Body);
    TSet<FString> Seen;
    for (const auto& Tag : Properties.Properties)
    {
        const FString Name = Native(Tag.Name);
        FProperty* Field = Schema->FindPropertyByName(FName(*Name));
        FString Issue;
        const FString Key = Name +
            (Tag.ArrayIndex ? FString::Printf(TEXT("[%llu]"), uint64(Tag.ArrayIndex)) : TEXT(""));
        if (Seen.Contains(Key.ToLower()))
            return Refuse(Error, Key, TEXT("duplicate original property"));
        Seen.Add(Key.ToLower());
        if (Skip.Contains(Name))
            continue; // the caller reads this one itself
        if (!Field && AbsentAsNotHeld)
        {
            P4MapKit::FDecodedValue Absent;
            if (!SourceValue(Tag, Body, Decoder, Class, Absent, Error))
                return false;
            Pending.NotHeld.Add(Key + TEXT(" = ") + Native(P4MapKit::DecodedJson(Absent)));
            continue;
        }
        const bool SchemaMatches = EnvelopeSchema(Field, Tag, Issue);
        if (!SchemaMatches)
            Pending.NativePropertyIssues.Add(Issue);
        P4MapKit::FDecodedValue Value;
        if (!SourceValue(Tag, Body, Decoder, Class, Value, Error))
            return false;
        const bool GroupFits = !Adapt || Adapt(Key, Value, Pending, Issue);
        FP4TexturePropertyDocument NativeDependencies;
        if (!SchemaMatches || !GroupFits || !Dependencies(Field, Value, Key, NativeDependencies, Issue))
        {
            if (SchemaMatches)
                Pending.NativePropertyIssues.Add(Issue);
            PreserveSourceReferences(Tag, Value, Key, Pending);
        }
        else
            Pending.Dependencies.Append(NativeDependencies.Dependencies);
        Values.Fields.emplace_back(TCHAR_TO_UTF8(*Key), MoveTemp(Value));
        Pending.ExplicitFields.Add(Key);
    }
    for (TFieldIterator<FProperty> Field(Schema); Field; ++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_SkipSerialization))
            continue;
        auto& Missing = Field->HasAnyPropertyFlags(CPF_EditorOnly) ? Pending.MissingEditorSourceSettings
                                                                   : Pending.MissingRuntimeDefaults;
        for (int32 Index = 0; Index < Field->ArrayDim; ++Index)
        {
            const FString Key = Field->GetName() + (Index ? FString::Printf(TEXT("[%d]"), Index) : TEXT(""));
            if (!Seen.Contains(Key.ToLower()))
                Missing.Add(Key);
        }
    }
    Pending.ExplicitFields.Sort();
    Pending.MissingRuntimeDefaults.Sort();
    Pending.MissingEditorSourceSettings.Sort();
    Pending.ValuesJson = Native(P4MapKit::DecodedJson(Values));
    Pending.TaggedPropertiesQualified = true;
    Pending.NativePropertiesRepresentable = Pending.NativePropertyIssues.Num() == 0;
    Document = MoveTemp(Pending);
    return true;
}

bool P4PrepareTextureProperties(
    UTexture* Scratch, const FP4TexturePropertyDocument& Document, FString& Canonical, FString& Error)
{
    Canonical.Empty();
    if (!Document.TaggedPropertiesQualified || !IsValid(Scratch) ||
        Scratch->GetClass()->GetPathName() != Document.ClassPath)
        return Refuse(
            Error, TEXT("Texture"), TEXT("source-qualified tagged properties and exact texture class required"));
    if (!Document.NativePropertiesRepresentable)
        return Refuse(
            Error, TEXT("Texture"), TEXT("original properties cannot be represented by installed native schema"));
    return P4PrepareExactPrivateProperties(Scratch, Document.ValuesJson, Canonical, Error);
}

bool P4PrepareObjectProperties(
    UObject* Scratch, const FP4TexturePropertyDocument& Document, FString& Canonical, FString& Error)
{
    Canonical.Empty();
    if (!Document.TaggedPropertiesQualified || !IsValid(Scratch) ||
        Scratch->GetClass()->GetPathName() != Document.ClassPath)
        return Refuse(Error, TEXT("Object"), TEXT("source-qualified tagged properties and exact class required"));
    if (!Document.NativePropertiesRepresentable)
        return Refuse(
            Error, TEXT("Object"), TEXT("original properties cannot be represented by installed native schema"));
    return P4PrepareExactPrivateProperties(Scratch, Document.ValuesJson, Canonical, Error);
}

void P4NameGameTextureGroups()
{
    // The engine reads a group's display name from [EnumRemap] in the engine ini each time it shows it.
    // The section is changed in memory only, so nothing is written to the project's files.
    FConfigSection* Remap = GConfig ? GConfig->GetSectionPrivate(TEXT("EnumRemap"), true, true, GEngineIni) : nullptr;
    if (!Remap)
        return;
    for (int32 Index = 0; Index < 15; ++Index)
    {
        const FName Key(*FString::Printf(TEXT("TEXTUREGROUP_Project%02d.DisplayName"), Index + 1));
        if (!Remap->Contains(Key))
            Remap->Add(Key, FConfigValue(GameTextureGroups[Index]));
    }
}

TSharedPtr<FJsonObject> P4TexturePropertyReport(const FP4TexturePropertyDocument& Document)
{
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("tagged_properties_qualified"), Document.TaggedPropertiesQualified);
    Report->SetBoolField(TEXT("source_tagged_properties_decoded"), Document.TaggedPropertiesQualified);
    Report->SetBoolField(TEXT("native_properties_representable"), Document.NativePropertiesRepresentable);
    Report->SetArrayField(TEXT("native_property_issues"), Strings(Document.NativePropertyIssues));
    Report->SetBoolField(TEXT("original_defaults_qualified"), false);
    Report->SetBoolField(TEXT("original_editor_source_settings_recovered"), false);
    Report->SetBoolField(TEXT("editable_import_qualified"), false);
    Report->SetStringField(TEXT("class"), Document.ClassPath);
    Report->SetStringField(TEXT("package"), Document.Package);
    Report->SetStringField(TEXT("values_json"), Document.ValuesJson);
    Report->SetStringField(TEXT("game_texture_group"), Document.GameTextureGroup);
    Report->SetArrayField(TEXT("explicit_fields"), Strings(Document.ExplicitFields));
    Report->SetArrayField(TEXT("missing_runtime_defaults"), Strings(Document.MissingRuntimeDefaults));
    Report->SetArrayField(TEXT("missing_editor_source_settings"), Strings(Document.MissingEditorSourceSettings));
    TArray<TSharedPtr<FJsonValue>> References;
    for (const auto& Item : Document.Dependencies)
    {
        auto Reference = MakeShared<FJsonObject>();
        Reference->SetStringField(TEXT("field"), Item.Field);
        Reference->SetStringField(TEXT("path"), Item.Path);
        Reference->SetStringField(TEXT("expected_class"), Item.ExpectedClass);
        Reference->SetBoolField(TEXT("original_expected_class_qualified"), false);
        Reference->SetStringField(TEXT("expected_class_provenance"),
            Item.ExpectedClass.IsEmpty() ? TEXT("unqualified") : TEXT("installed-engine-property"));
        Reference->SetBoolField(TEXT("soft"), Item.Soft);
        References.Add(MakeShared<FJsonValueObject>(Reference));
    }
    Report->SetArrayField(TEXT("dependencies"), References);
    return Report;
}

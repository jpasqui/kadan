#include "P4TextureProperties.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS)
#include "P4AuthoringRows.h"
#include "P4ImportReferences.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureCube.h"

namespace P4TextureCoverage
{
using namespace P4MapKit;
// Same owned in-memory package/import convention as TexturePropertiesAutomation.
// Payloads come from installed reflected SerializeItem implementations. The
// bounded package-name/outer tag writer follows the existing fixture convention;
// native struct/array/enum payload encodings are not reimplemented here.
struct FTaggedWriter : FMemoryWriter
{
    FPackageInventory& Package;
    UObject* Dependency;
    FTaggedWriter(TArray<uint8>& Bytes, FPackageInventory& InPackage, UObject* InDependency)
        : FMemoryWriter(Bytes, true), Package(InPackage), Dependency(InDependency)
    {
        SetUE4Ver(VER_UE4_AUTOMATIC_VERSION);
    }
    using FMemoryWriter::operator<<;
    virtual FArchive& operator<<(FName& Value) override
    {
        const std::string Plain = TCHAR_TO_UTF8(*Value.GetPlainNameString());
        const auto Found = std::find(Package.Names.begin(), Package.Names.end(), Plain);
        int32 Index = int32(Found - Package.Names.begin()), Number = Value.GetNumber();
        if (Found == Package.Names.end())
        {
            Index = int32(Package.Names.size());
            Package.Names.push_back(Plain);
        }
        FArchive& Self = *this;
        Self << Index << Number;
        return *this;
    }
    virtual FArchive& operator<<(UObject*& Value) override
    {
        int32 Index = Value ? -4 : 0;
        if (Value && Value != Dependency)
        {
            SetError();
            return *this;
        }
        FArchive& Self = *this;
        Self << Index;
        return *this;
    }
};
const TArray<FName> Settings{TEXT("LightingGuid"), TEXT("SRGB"), TEXT("NeverStream"), TEXT("LODBias"), TEXT("Filter"),
    TEXT("MipLoadOptions"), TEXT("DownscaleOptions"), TEXT("AddressX"), TEXT("AddressY"), TEXT("AddressZ"),
    TEXT("AdjustBrightness"), TEXT("AdjustHue"), TEXT("PaddingColor"), TEXT("AlphaCoverageThresholds"),
    TEXT("LayerFormatSettings"), TEXT("SourceTextures"), TEXT("CompositeTexture")};
struct FFixture
{
    FPackageInventory Package;
    FPackageExport Export;
    FPropertyInventory Properties;
    std::vector<std::uint8_t> Body;
    bool Build(UTexture2DArray* Texture, UObject* Dependency, const TArray<FName>& Fields = Settings,
        bool PropertyGuid = false)
    {
        Package = FPackageInventory();
        Package.Package = "/Game/P4TextureCoverageFixture";
        Package.Imports.push_back({"CoreUObject", "Package", "/Script/Engine", 0});
        Package.Imports.push_back({"CoreUObject", "Class", "Texture2DArray", -1});
        Package.Imports.push_back({"CoreUObject", "Package", "/Game/TextureDependency", 0});
        Package.Imports.push_back({"Engine", "Texture2D", "TextureDependency", -3});
        TArray<uint8> Bytes;
        FTaggedWriter Writer(Bytes, Package, Dependency);
        for (FName Name : Fields)
        {
            FProperty* Field = Texture->GetClass()->FindPropertyByName(Name);
            if (!Field)
                return false;
            uint8* Value = Field->ContainerPtrToValuePtr<uint8>(Texture);
            FArchive& Archive = Writer;
            FName Type = Field->GetID();
            Archive << Name << Type;
            const int64 SizeOffset = Writer.Tell();
            int32 Size = 0, Slot = 0;
            Archive << Size << Slot;
            if (auto Struct = CastField<FStructProperty>(Field))
            {
                FName Detail = Struct->Struct->GetFName();
                FGuid Guid = Struct->Struct->GetCustomGuid();
                Archive << Detail << Guid;
            }
            else if (auto Byte = CastField<FByteProperty>(Field))
            {
                FName Detail = Byte->Enum ? Byte->Enum->GetFName() : NAME_None;
                Archive << Detail;
            }
            else if (auto Enum = CastField<FEnumProperty>(Field))
            {
                FName Detail = Enum->GetEnum()->GetFName();
                Archive << Detail;
            }
            else if (auto Array = CastField<FArrayProperty>(Field))
            {
                FName Detail = Array->Inner->GetID();
                Archive << Detail;
            }
            auto Boolean = CastField<FBoolProperty>(Field);
            if (Boolean)
            {
                uint8 Flag = Boolean->GetPropertyValue(Value) ? 1 : 0;
                Archive << Flag;
            }
            uint8 HasGuid = PropertyGuid && Name == TEXT("LightingGuid") ? 1 : 0;
            Archive << HasGuid;
            if (HasGuid)
            {
                FGuid Guid(1, 2, 3, 4);
                Archive << Guid;
            }
            const int64 Start = Writer.Tell();
            if (!Boolean)
                Field->SerializeItem(FStructuredArchiveFromArchive(Writer).GetSlot(), Value, nullptr);
            const int64 End = Writer.Tell();
            Size = int32(End - Start);
            Writer.Seek(SizeOffset);
            Archive << Size;
            Writer.Seek(End);
        }
        FName None = NAME_None;
        Writer << None;
        if (Writer.IsError())
            return false;
        Writer.Close();
        Body.assign(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
        Export = FPackageExport();
        Export.Class = -2;
        Export.Name = "Fixture";
        Export.Size = Body.size();
        Properties = ReadPropertyEnvelopes(Body, Package.Names, 0, Body.size());
        return Properties.Error.Code.empty();
    }
    bool Inspect(FP4TexturePropertyDocument& Out, FString& Error) const
    {
        return P4InspectTextureProperties(
            Package, Export, Properties, Body, UTexture2DArray::StaticClass(), Out, Error);
    }
};
void Configure(UTexture2DArray* Texture, UTexture2D* Dependency)
{
    Texture->SRGB = false;
    Texture->NeverStream = true;
    Texture->LODBias = -3;
    Texture->Filter = TF_Nearest;
    Texture->MipLoadOptions = ETextureMipLoadOptions::OnlyFirstMip;
    Texture->DownscaleOptions = ETextureDownscaleOptions::Sharpen2;
    Texture->AddressX = TA_Mirror;
    Texture->AddressY = TA_Clamp;
    Texture->AddressZ = TA_Wrap;
    Texture->AdjustBrightness = 1.25f;
    Texture->AdjustHue = 37.5f;
    Texture->PaddingColor = FColor(19, 41, 73, 109);
    Texture->AlphaCoverageThresholds = FVector4(0.125f, 0.25f, 0.5f, 0.75f);
    FTextureFormatSettings Layer;
    Layer.CompressionSettings = TC_Masks;
    Layer.CompressionNoAlpha = true;
    Layer.CompressionNone = true;
    Layer.CompressionYCoCg = true;
    Layer.SRGB = true;
    Texture->LayerFormatSettings = {Layer};
    Texture->SourceTextures = {Dependency, nullptr};
    Texture->CompositeTexture = nullptr;
    auto Guid = CastField<FStructProperty>(Texture->GetClass()->FindPropertyByName(TEXT("LightingGuid")));
    if (Guid)
        *Guid->ContainerPtrToValuePtr<FGuid>(Texture) = FGuid(UINT32_MAX, 0x80000000u, 0x11223344u, 0u);
}
bool NativeEquals(UTexture2DArray* Texture, UTexture2D* Dependency)
{
    if (Texture->SRGB || !Texture->NeverStream || Texture->LODBias != -3 || Texture->Filter != TF_Nearest ||
        Texture->MipLoadOptions != ETextureMipLoadOptions::OnlyFirstMip ||
        Texture->DownscaleOptions != ETextureDownscaleOptions::Sharpen2 || Texture->AddressX != TA_Mirror ||
        Texture->AddressY != TA_Clamp || Texture->AddressZ != TA_Wrap || Texture->AdjustBrightness != 1.25f ||
        Texture->AdjustHue != 37.5f || Texture->PaddingColor != FColor(19, 41, 73, 109) ||
        Texture->AlphaCoverageThresholds != FVector4(0.125f, 0.25f, 0.5f, 0.75f) ||
        Texture->LayerFormatSettings.Num() != 1 || Texture->SourceTextures.Num() != 2 ||
        Texture->SourceTextures[0] != Dependency || Texture->SourceTextures[1] != nullptr ||
        Texture->CompositeTexture != nullptr ||
        Texture->GetLightingGuid() != FGuid(UINT32_MAX, 0x80000000u, 0x11223344u, 0u))
        return false;
    const auto& Layer = Texture->LayerFormatSettings[0];
    return Layer.CompressionSettings == TC_Masks && Layer.CompressionNoAlpha && Layer.CompressionNone &&
        Layer.CompressionYCoCg && Layer.SRGB && !Texture->Source.IsValid() && Texture->PlatformData == nullptr &&
        Texture->Resource == nullptr;
}
bool SameState(UTexture2DArray* Texture, UObject* Dependency, const FFixture& Before)
{
    FFixture After;
    return After.Build(Texture, Dependency) && Before.Body == After.Body &&
        Before.Package.Names == After.Package.Names && !Texture->Source.IsValid() && Texture->PlatformData == nullptr &&
        Texture->Resource == nullptr;
}
bool NoAdmission(const FP4TexturePropertyDocument& Document)
{
    const auto Report = P4TexturePropertyReport(Document);
    return !Report->GetBoolField(TEXT("original_defaults_qualified")) &&
        !Report->GetBoolField(TEXT("original_editor_source_settings_recovered")) &&
        !Report->GetBoolField(TEXT("editable_import_qualified"));
}
} //namespace P4TextureCoverage

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureSettingsNativeShapesTest, "P4MapKit.Quality.TextureSettingsNativeShapes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureSettingsNativeShapesTest::RunTest(const FString&)
{
    using namespace P4TextureCoverage;
    TStrongObjectPtr<UTexture2D> Dependency(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
    TStrongObjectPtr<UTexture2DArray> Original(
        NewObject<UTexture2DArray>(GetTransientPackage(), NAME_None, RF_Transient));
    TStrongObjectPtr<UTexture2DArray> Scratch(
        NewObject<UTexture2DArray>(GetTransientPackage(), NAME_None, RF_Transient));
    Scratch->CompositeTexture = Dependency.Get();
    Scratch->SourceTextures = {Dependency.Get(), Dependency.Get(), Dependency.Get()};
    Configure(Original.Get(), Dependency.Get());
    FFixture Source;
    FP4TexturePropertyDocument Document;
    FString Error, Canonical;
    if (!TestTrue(TEXT("native engine serializes controlled texture struct/array/enum payloads"),
            Source.Build(Original.Get(), Dependency.Get())))
        return false;
    const bool Inspected = Source.Inspect(Document, Error);
    AddInfo(FString::Printf(
        TEXT("Texture native shape diagnostic: inspected=%d representable=%d error=%s issues=%s values=%s"),
        Inspected ? 1 : 0, Document.NativePropertiesRepresentable ? 1 : 0, *Error,
        *FString::Join(Document.NativePropertyIssues, TEXT(" | ")), *Document.ValuesJson));
    if (!TestTrue(TEXT("exact native source shapes independently inspect"),
            Inspected && Document.NativePropertiesRepresentable))
        return false;
    if (!TestTrue(TEXT("null composite/array entry adds no fictitious dependency"),
            Document.Dependencies.Num() == 1 && Document.Dependencies[0].Field == TEXT("SourceTextures[0]") &&
                Document.Dependencies[0].ExpectedClass == TEXT("/Script/Engine.Texture2D")))
        return false;
    TSharedPtr<FJsonObject> OriginalValues;
    if (!TestTrue(TEXT("original scalar and array null source values remain explicit JSON null"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Document.ValuesJson), OriginalValues) &&
                OriginalValues.IsValid() && OriginalValues->HasTypedField<EJson::Null>(TEXT("CompositeTexture")) &&
                OriginalValues->HasTypedField<EJson::Array>(TEXT("SourceTextures")) &&
                OriginalValues->GetArrayField(TEXT("SourceTextures")).Num() == 2 &&
                OriginalValues->GetArrayField(TEXT("SourceTextures"))[1]->Type == EJson::Null))
        return false;
    const FString SourceJsonBefore = Document.ValuesJson;
    // A null archive must actively clear a preexisting pointer and array slot,
    // not just leave freshly constructed null defaults untouched.
    Scratch->CompositeTexture = Dependency.Get();
    Scratch->SourceTextures = {Dependency.Get(), Dependency.Get(), Dependency.Get()};
    TMap<FString, UObject*> References;
    References.Add(TEXT("/Game/TextureDependency.TextureDependency"), Dependency.Get());
    FP4ImportReferenceScope Scope(References);
    return TestTrue(TEXT("native shapes stage into private texture without defaults/build admission"),
               P4PrepareTextureProperties(Scratch.Get(), Document, Canonical, Error)) &&
        TestTrue(
            TEXT(
                "independent native members preserve byte enum/class enum, array struct flags, references, GUID bits and scalar settings"),
            NativeEquals(Scratch.Get(), Dependency.Get())) &&
        TestTrue(
            TEXT(
                "explicit source null clears prior nonnull scalar/array references and removes stale array tail without rewriting original document"),
            Scratch->CompositeTexture == nullptr && Scratch->SourceTextures.Num() == 2 &&
                Scratch->SourceTextures[1] == nullptr && Document.ValuesJson == SourceJsonBefore) &&
        TestTrue(TEXT("qualified explicit settings leave original runtime/editor omissions visible"),
            Document.MissingRuntimeDefaults.Num() > 0 && Document.MissingEditorSourceSettings.Num() > 0 &&
                NoAdmission(Document));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureSettingsSourceRefusalsTest, "P4MapKit.Quality.TextureSettingsSourceRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureSettingsSourceRefusalsTest::RunTest(const FString&)
{
    using namespace P4TextureCoverage;
    TStrongObjectPtr<UTexture2D> Dependency(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
    TStrongObjectPtr<UTexture2DArray> Original(
        NewObject<UTexture2DArray>(GetTransientPackage(), NAME_None, RF_Transient));
    TStrongObjectPtr<UTexture2DArray> Scratch(
        NewObject<UTexture2DArray>(GetTransientPackage(), NAME_None, RF_Transient));
    Configure(Original.Get(), Dependency.Get());
    Configure(Scratch.Get(), Dependency.Get());
    FFixture Source, Before;
    if (!TestTrue(TEXT("real native source and full tested before-state serialize"),
            Source.Build(Original.Get(), Dependency.Get()) && Before.Build(Scratch.Get(), Dependency.Get())))
        return false;
    for (int32 Case = 0; Case < 22; ++Case)
    {
        auto Changed = Source;
        UClass* Schema = UTexture2DArray::StaticClass();
        if (Case == 0)
            Schema = nullptr;
        else if (Case == 1)
            Schema = UTexture::StaticClass();
        else if (Case == 2)
            Changed.Package.Error.Code = "owned-malformed-package";
        else if (Case == 3)
            Changed.Properties.Error.Code = "owned-malformed-properties";
        else if (Case == 4)
            Changed.Export.Class = 99;
        else if (Case == 5)
            Changed.Export.Offset = Changed.Body.size() + 1;
        else if (Case == 6)
            Changed.Export.Size = Changed.Body.size() + 1;
        else if (Case == 7)
            Changed.Properties.End = Changed.Body.size() + 1;
        else if (Case == 8)
            Changed.Properties.End--;
        else if (Case == 9)
            Changed.Properties.Properties.pop_back();
        else if (Case == 10)
            Changed.Body[0] = Changed.Body[1] = Changed.Body[2] = Changed.Body[3] = 0xff;
        else
        {
            auto& Tag = Changed.Properties.Properties[0];
            switch (Case - 11)
            {
                case 0:
                    Tag.Name += "Changed";
                    break;
                case 1:
                    Tag.Type = "IntProperty";
                    break;
                case 2:
                    Tag.Detail[0] = "Color";
                    break;
                case 3:
                    ++Tag.TagOffset;
                    break;
                case 4:
                    ++Tag.PayloadOffset;
                    break;
                case 5:
                    --Tag.Size;
                    break;
                case 6:
                    ++Tag.ArrayIndex;
                    break;
                case 7:
                    Tag.BoolValue = !Tag.BoolValue;
                    break;
                case 8:
                    Tag.HasGuid = !Tag.HasGuid;
                    break;
                case 9:
                    Tag.StructGuid[0] ^= 1;
                    break;
                case 10:
                    Tag.PropertyGuid[0] ^= 1;
                    break;
            }
        }
        FP4TexturePropertyDocument Refused;
        Refused.TaggedPropertiesQualified = true;
        Refused.ValuesJson = TEXT("stale qualified output");
        FString Error;
        if (!TestTrue(TEXT("malformed or replaced source cannot retain previous qualified output"),
                !P4InspectTextureProperties(
                    Changed.Package, Changed.Export, Changed.Properties, Changed.Body, Schema, Refused, Error) &&
                    !Refused.TaggedPropertiesQualified && !Refused.NativePropertiesRepresentable &&
                    Refused.ValuesJson.IsEmpty() && !Error.IsEmpty()) ||
            !TestTrue(TEXT("source refusal does not mutate any tested native setting/reference/resource"),
                SameState(Scratch.Get(), Dependency.Get(), Before)))
            return false;
    }
    FFixture Guid;
    FP4TexturePropertyDocument Refused;
    FString Error;
    if (!TestTrue(TEXT("controlled property GUID envelope retains actual native GUID payload"),
            Guid.Build(Original.Get(), Dependency.Get(), Settings, true)) ||
        !TestTrue(TEXT("unqualified property GUID refuses without discarding source GUID identity"),
            !Guid.Inspect(Refused, Error) && !Refused.TaggedPropertiesQualified) ||
        !TestTrue(TEXT("property GUID refusal preserves complete tested native before-state"),
            SameState(Scratch.Get(), Dependency.Get(), Before)))
        return false;
    FFixture Duplicate;
    const bool DuplicateAccepted = Duplicate.Build(Original.Get(), Dependency.Get(), {TEXT("SRGB"), TEXT("SRGB")});
    AddInfo(FString::Printf(TEXT("Texture duplicate source diagnostic: accepted=%d parser_error=%s at=%llu"),
        DuplicateAccepted ? 1 : 0, UTF8_TO_TCHAR(Duplicate.Properties.Error.Code.c_str()),
        uint64(Duplicate.Properties.Error.Offset)));
    return TestTrue(TEXT("actual source parser refuses duplicate native bool envelopes before inspector admission"),
               !DuplicateAccepted && Duplicate.Properties.Error.Code == "duplicate-property") &&
        TestTrue(TEXT("duplicate source settings refuse rather than silently choose last value"),
            !Duplicate.Inspect(Refused, Error) && !Refused.TaggedPropertiesQualified) &&
        TestTrue(TEXT("duplicate source refusal preserves native before-state"),
            SameState(Scratch.Get(), Dependency.Get(), Before));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureSettingsReferenceRefusalsTest,
    "P4MapKit.Quality.TextureSettingsReferenceRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureSettingsReferenceRefusalsTest::RunTest(const FString&)
{
    using namespace P4TextureCoverage;
    TStrongObjectPtr<UTexture2D> Dependency(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
    TStrongObjectPtr<UTextureCube> WrongClass(NewObject<UTextureCube>(GetTransientPackage(), NAME_None, RF_Transient));
    TStrongObjectPtr<UTexture2DArray> Original(
        NewObject<UTexture2DArray>(GetTransientPackage(), NAME_None, RF_Transient));
    TStrongObjectPtr<UTexture2DArray> Scratch(
        NewObject<UTexture2DArray>(GetTransientPackage(), NAME_None, RF_Transient));
    Configure(Original.Get(), Dependency.Get());
    Configure(Scratch.Get(), Dependency.Get());
    FFixture Source, Before;
    if (!TestTrue(TEXT("native reference fixture and complete tested before-state serialize"),
            Source.Build(Original.Get(), Dependency.Get()) && Before.Build(Scratch.Get(), Dependency.Get())))
        return false;
    FP4TexturePropertyDocument Document;
    FString Error, Canonical;
    const bool Inspected = Source.Inspect(Document, Error);
    AddInfo(
        FString::Printf(TEXT("Texture native reference diagnostic: inspected=%d representable=%d error=%s issues=%s"),
            Inspected ? 1 : 0, Document.NativePropertiesRepresentable ? 1 : 0, *Error,
            *FString::Join(Document.NativePropertyIssues, TEXT(" | "))));
    if (!TestTrue(TEXT("real declared array object class is source decoded and representable before reference faults"),
            Inspected && Document.NativePropertiesRepresentable))
        return false;
    for (int32 Case = 0; Case < 3; ++Case)
    {
        auto Changed = Document;
        TMap<FString, UObject*> References;
        if (Case == 0)
            References.Add(TEXT("/Game/TextureDependency.TextureDependency"), WrongClass.Get());
        if (Case == 1)
            Changed.TaggedPropertiesQualified = false;
        if (Case == 2)
            Changed.ClassPath = TEXT("/Script/Engine.TextureCube");
        FP4ImportReferenceScope Scope(References);
        Canonical = TEXT("previous canonical output");
        if (!TestTrue(TEXT("wrong-class/missing source authority refuses before private texture writes"),
                !P4PrepareTextureProperties(Scratch.Get(), Changed, Canonical, Error) && Canonical.IsEmpty()) ||
            !TestTrue(TEXT("reference refusal preserves every tested field, array element, GUID and sibling resource"),
                SameState(Scratch.Get(), Dependency.Get(), Before)))
            return false;
    }
    // An unknown future source field is not new native support. Retain its exact
    // array references and forbid staging rather than borrow a target declaration.
    auto Unknown = Source;
    for (auto& Name : Unknown.Package.Names)
        if (Name == "SourceTextures")
            Name = "OriginalOnlyTextureArray";
    Unknown.Properties = ReadPropertyEnvelopes(Unknown.Body, Unknown.Package.Names, 0, Unknown.Body.size());
    if (!TestTrue(TEXT("unknown original array retains exact decoded references with unqualified declared class"),
            Unknown.Inspect(Document, Error) && !Document.NativePropertiesRepresentable &&
                Document.Dependencies.Num() == 1 &&
                Document.Dependencies[0].Field == TEXT("OriginalOnlyTextureArray[0]") &&
                Document.Dependencies[0].ExpectedClass.IsEmpty() &&
                Document.Dependencies[0].Path == TEXT("/Game/TextureDependency.TextureDependency") &&
                NoAdmission(Document)))
        return false;
    const auto Report = P4TexturePropertyReport(Document);
    const auto DependencyReport = Report->GetArrayField(TEXT("dependencies"))[0]->AsObject();
    if (!TestTrue(TEXT("report preserves unqualified original array class provenance"),
            DependencyReport->GetStringField(TEXT("expected_class_provenance")) == TEXT("unqualified") &&
                !DependencyReport->GetBoolField(TEXT("original_expected_class_qualified"))))
        return false;
    Canonical = TEXT("previous canonical output");
    return TestTrue(TEXT("unrepresented source array blocks staging without fabricated fields/defaults"),
               !P4PrepareTextureProperties(Scratch.Get(), Document, Canonical, Error) && Canonical.IsEmpty()) &&
        TestTrue(TEXT("unrepresented source array leaves prior complete tested native state unchanged"),
            SameState(Scratch.Get(), Dependency.Get(), Before));
}
#endif

// PRIVATE exact-batch110 draft. Native execution and promotion belong to root.
#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "P4TextureProperties.h"
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace P4TexturePropertyCoverageNext2
{
struct FP4TexturePropertyNext2SourceFixture
{
    P4MapKit::FPackageInventory Package;
    TArray<uint8> Archive;
    FMemoryWriter Writer;
    P4MapKit::FPackageExport Export;
    std::vector<uint8> Body;
    P4MapKit::FPropertyInventory Properties;
    explicit FP4TexturePropertyNext2SourceFixture(int32 Prefix = 0) : Writer(Archive, true)
    {
        Package.Package = "/Game/P4TexturePropertyCoverageFixture";
        Package.Imports.push_back({"CoreUObject", "Package", "/Script/Engine", 0});
        Package.Imports.push_back({"CoreUObject", "Class", "Texture2D", -1});
        Package.Imports.push_back({"CoreUObject", "Package", "/Game/SourceUserData", 0});
        Package.Imports.push_back({"Engine", "AssetUserData", "SourceUserData", -3});
        Export.Class = -2;
        Export.Name = "TextureFixture";
        Export.Offset = std::size_t(Prefix);
        for (int32 Index = 0; Index < Prefix; ++Index)
        {
            uint8 Zero = 0;
            Writer << Zero;
        }
    }
    void Name(FArchive& Out, const TCHAR* Text)
    {
        const std::string UTF8 = TCHAR_TO_UTF8(Text);
        const auto Found = std::find(Package.Names.begin(), Package.Names.end(), UTF8);
        int32 Index = int32(Found - Package.Names.begin()), Number = 0;
        if (Found == Package.Names.end())
        {
            Index = int32(Package.Names.size());
            Package.Names.push_back(UTF8);
        }
        Out << Index << Number;
    }
    void Tag(FArchive& Out, const TCHAR* Field, const TCHAR* Type, const TArray<uint8>& Payload,
        const TCHAR* Detail = nullptr, const TCHAR* SecondDetail = nullptr, int32 Slot = 0, bool Boolean = false,
        bool HasGuid = false)
    {
        Name(Out, Field);
        Name(Out, Type);
        int32 Size = Payload.Num();
        Out << Size << Slot;
        if (Detail)
            Name(Out, Detail);
        if (SecondDetail)
            Name(Out, SecondDetail);
        if (FString(Type) == TEXT("StructProperty"))
        {
            FGuid StructGuid;
            Out << StructGuid;
        }
        if (FString(Type) == TEXT("BoolProperty"))
        {
            uint8 Flag = Boolean ? 1 : 0;
            Out << Flag;
        }
        uint8 GuidFlag = HasGuid ? 1 : 0;
        Out << GuidFlag;
        if (HasGuid)
        {
            FGuid PropertyGuid(1, 2, 3, 4);
            Out << PropertyGuid;
        }
        if (Payload.Num())
            Out.Serialize(const_cast<uint8*>(Payload.GetData()), Payload.Num());
    }
    void Finish()
    {
        Name(Writer, TEXT("None"));
        Writer.Close();
        Body.assign(Archive.GetData(), Archive.GetData() + Archive.Num());
        Export.Size = Body.size() - Export.Offset;
        Properties = P4MapKit::ReadPropertyEnvelopes(Body, Package.Names, Export.Offset, Export.Offset + Export.Size);
    }
    bool Inspect(FP4TexturePropertyDocument& Doc, FString& Error)
    {
        return P4InspectTextureProperties(Package, Export, Properties, Body, UTexture2D::StaticClass(), Doc, Error);
    }
    TArray<uint8> Named(const TCHAR* Value)
    {
        TArray<uint8> Result;
        FMemoryWriter Payload(Result, true);
        Name(Payload, Value);
        return Result;
    }
    void Soft(FArchive& Payload, const TCHAR* Path)
    {
        Name(Payload, Path);
        FString Empty;
        Payload << Empty;
    }
};
TArray<uint8> Snapshot(UObject* Object)
{
    TArray<uint8> Bytes;
    FObjectWriter Writer(Object, Bytes, false, false, false, PPF_DuplicateVerbatim);
    return Bytes;
}
void EmptyDocument(FAutomationTestBase& Test, const FP4TexturePropertyDocument& Doc)
{
    Test.TestTrue(TEXT("Refusal clears source qualifications and previous document"),
        !Doc.TaggedPropertiesQualified && !Doc.NativePropertiesRepresentable && Doc.ClassPath.IsEmpty() &&
            Doc.ValuesJson.IsEmpty() && Doc.ExplicitFields.Num() == 0 && Doc.Dependencies.Num() == 0 &&
            Doc.NativePropertyIssues.Num() == 0);
}
void RefusesUnchanged(FAutomationTestBase& Test, UTexture2D* Texture, const FP4TexturePropertyDocument& Doc)
{
    const auto Bytes = Snapshot(Texture);
    const EObjectFlags Flags = Texture->GetFlags();
    const bool Dirty = Texture->GetOutermost()->IsDirty();
    const FGuid Guid = Texture->GetLightingGuid();
    const bool SRGB = Texture->SRGB;
    int32 Pre = 0, Post = 0;
    const auto A = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
        [&](UObject* Target, const FEditPropertyChain&)
        {
            if (Target == Texture)
                ++Pre;
        });
    const auto B = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
        [&](UObject* Target, FPropertyChangedEvent&)
        {
            if (Target == Texture)
                ++Post;
        });
    FString Canonical(TEXT("prior")), Error;
    Test.TestFalse(TEXT("Unrepresented/unqualified source never stages"),
        P4PrepareTextureProperties(Texture, Doc, Canonical, Error));
    FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(A);
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(B);
    Test.TestTrue(TEXT("Refusal clears canonical output and reports reason"), Canonical.IsEmpty() && !Error.IsEmpty());
    Test.TestTrue(TEXT("Refusal retains full native serialization, flags and dirty state"),
        Snapshot(Texture) == Bytes && Texture->GetFlags() == Flags && Texture->GetOutermost()->IsDirty() == Dirty);
    Test.TestTrue(TEXT("Independent native GUID and sRGB remain exact"),
        Texture->GetLightingGuid() == Guid && Texture->SRGB == SRGB);
    Test.TestTrue(TEXT("Refusal fires no pre/post callbacks"), Pre == 0 && Post == 0);
}
void ReportGaps(FAutomationTestBase& Test, const FP4TexturePropertyDocument& Doc)
{
    auto Report = P4TexturePropertyReport(Doc);
    Test.TestTrue(
        TEXT("Exact source text is retained in report"), Report->GetStringField(TEXT("values_json")) == Doc.ValuesJson);
    Test.TestTrue(TEXT("Source success does not admit originals/defaults/full editable assets"),
        !Report->GetBoolField(TEXT("original_defaults_qualified")) &&
            !Report->GetBoolField(TEXT("original_editor_source_settings_recovered")) &&
            !Report->GetBoolField(TEXT("editable_import_qualified")));
}
} //namespace P4TexturePropertyCoverageNext2

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TexturePropertySourceRangeCoverage,
    "P4MapKit.Quality.TexturePropertySourceRangeCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TexturePropertySourceRangeCoverage::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TexturePropertyCoverageNext2;
    FP4TexturePropertyNext2SourceFixture Source(8);
    TArray<uint8> Empty;
    Source.Tag(Source.Writer, TEXT("SRGB"), TEXT("BoolProperty"), Empty, nullptr, nullptr, 0, false);
    Source.Finish();
    FP4TexturePropertyDocument Doc;
    FString Error;
    if (!TestTrue(TEXT("Valid source starts after a real prefix"), Source.Inspect(Doc, Error)))
        return false;
    const auto Body = Source.Body;
    const auto Names = Source.Package.Names;
    const bool Refused = Async(EAsyncExecution::Thread,
        [&Source]()
        {
            FP4TexturePropertyDocument Out;
            FString Reason;
            return !Source.Inspect(Out, Reason) && !Out.TaggedPropertiesQualified && Out.ValuesJson.IsEmpty();
        }).Get();
    TestTrue(TEXT("Texture source inspection refuses worker before native schema traversal"), Refused);
    auto LowerEnd = Source.Properties;
    LowerEnd.End = Source.Export.Offset - 1;
    TestFalse(TEXT("Claimed inventory ending before source export refuses"),
        P4InspectTextureProperties(
            Source.Package, Source.Export, LowerEnd, Source.Body, UTexture2D::StaticClass(), Doc, Error));
    EmptyDocument(*this, Doc);
    TStrongObjectPtr<UTexture2D> Scratch(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
    Scratch->SRGB = true;
    Scratch->SetLightingGuid(FGuid(5, 6, 7, 8));
    RefusesUnchanged(*this, Scratch.Get(), Doc);
    FP4TexturePropertyDocument Valid;
    if (!TestTrue(TEXT("Original source can be independently inspected again"), Source.Inspect(Valid, Error)))
        return false;
    FString Canonical;
    TestFalse(TEXT("Source-qualified document does not accept null scratch"),
        P4PrepareTextureProperties(nullptr, Valid, Canonical, Error));
    TestTrue(TEXT("All source bytes/name entries retained"), Source.Body == Body && Source.Package.Names == Names);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TexturePropertyGuidArchiveCoverage,
    "P4MapKit.Quality.TexturePropertyGuidArchiveCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TexturePropertyGuidArchiveCoverage::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TexturePropertyCoverageNext2;
    const FGuid Original(0xffffffffu, 0x80000000u, 0x7fffffffu, 0x01234567u);
    FP4TexturePropertyNext2SourceFixture Valid;
    TArray<uint8> Bytes;
    {
        FMemoryWriter Out(Bytes, true);
        FGuid Value = Original;
        Out << Value;
    }
    Valid.Tag(Valid.Writer, TEXT("LightingGuid"), TEXT("StructProperty"), Bytes, TEXT("Guid"));
    Valid.Finish();
    FP4TexturePropertyDocument Doc;
    FString Error, Canonical;
    if (!TestTrue(TEXT("Actual native GUID archive words decode"), Valid.Inspect(Doc, Error)))
        return false;
    TestTrue(TEXT("Source GUID signed word tokens preserve every bit"),
        Doc.ValuesJson.Contains(TEXT("\"A\":-1")) && Doc.ValuesJson.Contains(TEXT("\"B\":-2147483648")) &&
            Doc.ValuesJson.Contains(TEXT("\"C\":2147483647")) && Doc.ValuesJson.Contains(TEXT("\"D\":19088743")));
    TStrongObjectPtr<UTexture2D> Scratch(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
    if (!TestTrue(
            TEXT("Source GUID privately stages"), P4PrepareTextureProperties(Scratch.Get(), Doc, Canonical, Error)))
        return false;
    TestTrue(
        TEXT("Independent installed native GUID accessor matches archive"), Scratch->GetLightingGuid() == Original);
    for (int32 Size : {15, 17})
    {
        FP4TexturePropertyNext2SourceFixture Bad;
        TArray<uint8> Payload;
        Payload.SetNumZeroed(Size);
        Bad.Tag(Bad.Writer, TEXT("LightingGuid"), TEXT("StructProperty"), Payload, TEXT("Guid"));
        Bad.Finish();
        TestTrue(TEXT("Malformed GUID still has complete valid outer envelope"), Bad.Properties.Error.Code.empty());
        Doc.TaggedPropertiesQualified = true;
        Doc.ValuesJson = TEXT("prior");
        TestFalse(TEXT("GUID cannot borrow/truncate native archive words"), Bad.Inspect(Doc, Error));
        TestTrue(
            TEXT("Exact GUID adapter reports archive refusal"), Error.Contains(TEXT("invalid Guid source archive")));
        EmptyDocument(*this, Doc);
        RefusesUnchanged(*this, Scratch.Get(), Doc);
    }
    FP4TexturePropertyNext2SourceFixture PropertyGuid;
    PropertyGuid.Tag(PropertyGuid.Writer, TEXT("LightingGuid"), TEXT("StructProperty"), Bytes, TEXT("Guid"), nullptr, 0,
        false, true);
    PropertyGuid.Finish();
    TestFalse(TEXT("Property-GUID serialization needs its own source adapter"), PropertyGuid.Inspect(Doc, Error));
    EmptyDocument(*this, Doc);
    FP4TexturePropertyNext2SourceFixture UnknownCodec;
    UnknownCodec.Tag(UnknownCodec.Writer, TEXT("LightingGuid"), TEXT("StructProperty"), Bytes, TEXT("Matrix"));
    UnknownCodec.Finish();
    TestFalse(TEXT("Existing explicitly unsupported native Matrix codec cannot qualify by byte shape"),
        UnknownCodec.Inspect(Doc, Error));
    TestTrue(
        TEXT("Unsupported source codec remains precise refusal"), Error.Contains(TEXT("unsupported-native-struct")));
    EmptyDocument(*this, Doc);
    RefusesUnchanged(*this, Scratch.Get(), Doc);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TexturePropertyNativeSchemaCoverage,
    "P4MapKit.Quality.TexturePropertyNativeSchemaCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TexturePropertyNativeSchemaCoverage::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TexturePropertyCoverageNext2;
    TStrongObjectPtr<UTexture2D> Scratch(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
    auto* Transient = UTexture2D::StaticClass()->FindPropertyByName(TEXT("bNotOfflineProcessed"));
    if (!TestTrue(TEXT("Installed real texture flag is already transient"),
            Transient && Transient->HasAnyPropertyFlags(CPF_Transient)))
        return false;
    const EPropertyFlags Flags = Transient->GetPropertyFlags();
    for (int32 Variant = 0; Variant < 7; ++Variant)
    {
        FP4TexturePropertyNext2SourceFixture Source;
        TArray<uint8> Payload;
        if (Variant == 0)
            Source.Tag(
                Source.Writer, TEXT("bNotOfflineProcessed"), TEXT("BoolProperty"), Payload, nullptr, nullptr, 0, true);
        if (Variant == 1)
            Source.Tag(Source.Writer, TEXT("SRGB"), TEXT("BoolProperty"), Payload, nullptr, nullptr, 1, false);
        if (Variant == 2)
        {
            FMemoryWriter Out(Payload, true);
            int32 X = -19, Y = 32;
            Out << X << Y;
            Source.Tag(Source.Writer, TEXT("LightingGuid"), TEXT("StructProperty"), Payload, TEXT("IntPoint"));
        }
        if (Variant == 3)
        {
            Payload = Source.Named(TEXT("TF_Default"));
            Source.Tag(Source.Writer, TEXT("LODGroup"), TEXT("ByteProperty"), Payload, TEXT("TextureFilter"));
        }
        if (Variant == 4)
        {
            Payload = Source.Named(TEXT("TEXTUREGROUP_World"));
            Source.Tag(Source.Writer, TEXT("MipLoadOptions"), TEXT("EnumProperty"), Payload, TEXT("TextureGroup"));
        }
        if (Variant == 5)
        {
            uint8 Numeric = 3;
            FMemoryWriter Out(Payload, true);
            Out << Numeric;
            Source.Tag(Source.Writer, TEXT("LODGroup"), TEXT("ByteProperty"), Payload, TEXT("TextureGroup"));
        }
        if (Variant == 6)
        {
            FMemoryWriter Out(Payload, true);
            int32 Count = 2, A = MIN_int32, B = MAX_int32;
            Out << Count << A << B;
            Source.Tag(Source.Writer, TEXT("AssetUserData"), TEXT("ArrayProperty"), Payload, TEXT("IntProperty"));
        }
        Source.Finish();
        FP4TexturePropertyDocument Doc;
        FString Error;
        if (!TestTrue(TEXT("Known source codec remains completely decoded despite native incompatibility"),
                Source.Inspect(Doc, Error)))
            continue;
        TestTrue(TEXT("Exactly one native mismatch retained"),
            Doc.TaggedPropertiesQualified && !Doc.NativePropertiesRepresentable && Doc.NativePropertyIssues.Num() == 1);
        const TCHAR* Expected[] = {TEXT("\"bNotOfflineProcessed\":true"), TEXT("\"SRGB[1]\":false"), TEXT("\"X\":-19"),
            TEXT("TF_Default"), TEXT("TEXTUREGROUP_World"), TEXT("\"LODGroup\":3"), TEXT("[-2147483648,2147483647]")};
        TestTrue(TEXT("Original unsupported value remains exact in source inventory"),
            Doc.ValuesJson.Contains(Expected[Variant]));
        RefusesUnchanged(*this, Scratch.Get(), Doc);
        ReportGaps(*this, Doc);
    }
    TestTrue(TEXT("Installed real native property flags were never changed"), Transient->GetPropertyFlags() == Flags);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TexturePropertySourceReferenceCoverage,
    "P4MapKit.Quality.TexturePropertySourceReferenceCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TexturePropertySourceReferenceCoverage::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TexturePropertyCoverageNext2;
    FP4TexturePropertyNext2SourceFixture Known;
    TArray<uint8> Payload;
    {
        FMemoryWriter Out(Payload, true);
        int32 Count = 3, Reference = -4, Null = 0;
        Out << Count << Reference << Null << Reference;
    }
    Known.Tag(Known.Writer, TEXT("AssetUserData"), TEXT("ArrayProperty"), Payload, TEXT("ObjectProperty"));
    Known.Finish();
    FP4TexturePropertyDocument Doc;
    FString Error;
    if (!TestTrue(TEXT("Actual native user-data array accepts typed source reference/null inventory"),
            Known.Inspect(Doc, Error)))
        return false;
    TestTrue(TEXT("Explicit null creates no dependency; repeated references retain each source index"),
        Doc.NativePropertiesRepresentable && Doc.Dependencies.Num() == 2 &&
            Doc.Dependencies[0].Field == TEXT("AssetUserData[0]") &&
            Doc.Dependencies[1].Field == TEXT("AssetUserData[2]") &&
            Doc.Dependencies[0].Path == TEXT("/Game/SourceUserData.SourceUserData") &&
            Doc.Dependencies[0].ExpectedClass == TEXT("/Script/Engine.AssetUserData"));
    auto Report = P4TexturePropertyReport(Doc);
    const auto NativeReference = Report->GetArrayField(TEXT("dependencies"))[0]->AsObject();
    TestTrue(TEXT("Declared installed class does not qualify original declared class"),
        !NativeReference->GetBoolField(TEXT("original_expected_class_qualified")) &&
            NativeReference->GetStringField(TEXT("expected_class_provenance")) == TEXT("installed-engine-property"));
    FP4TexturePropertyNext2SourceFixture Soft;
    Payload.Reset();
    {
        FMemoryWriter Out(Payload, true);
        int32 Count = 2;
        Out << Count;
        Soft.Soft(Out, TEXT("None"));
        Soft.Soft(Out, TEXT("/Game/SourceSoft.SourceSoft"));
    }
    Soft.Tag(Soft.Writer, TEXT("AssetUserData"), TEXT("ArrayProperty"), Payload, TEXT("SoftObjectProperty"));
    Soft.Finish();
    if (!TestTrue(TEXT("Supported soft-reference source codec preserves native inner-kind mismatch"),
            Soft.Inspect(Doc, Error)))
        return false;
    TestTrue(TEXT("Empty soft source dependency omitted, surviving path retains soft identity"),
        !Doc.NativePropertiesRepresentable && Doc.Dependencies.Num() == 1 &&
            Doc.Dependencies[0].Field == TEXT("AssetUserData[1]") &&
            Doc.Dependencies[0].Path == TEXT("/Game/SourceSoft.SourceSoft") && Doc.Dependencies[0].Soft &&
            Doc.Dependencies[0].ExpectedClass.IsEmpty());
    FP4TexturePropertyNext2SourceFixture Map;
    Payload.Reset();
    {
        FMemoryWriter Out(Payload, true);
        int32 Removed = 0, Count = 2, Reference = -4, Null = 0;
        Out << Removed << Count << Reference;
        Map.Soft(Out, TEXT("None"));
        Out << Null;
        Map.Soft(Out, TEXT("/Game/SourceSoft.SourceSoft"));
    }
    // Existing supported hard/soft scalar codecs. The genuine native field is an
    // array, so this source kind is retained/refused, never treated as a UE map.
    Map.Tag(Map.Writer, TEXT("AssetUserData"), TEXT("MapProperty"), Payload, TEXT("ObjectProperty"),
        TEXT("SoftObjectProperty"));
    Map.Finish();
    if (!TestTrue(TEXT("Fully supported source map codec is inventoried despite actual field-kind mismatch"),
            Map.Inspect(Doc, Error)))
        return false;
    TestTrue(TEXT("Both map sides preserve exact surviving typed paths without invented native class"),
        !Doc.NativePropertiesRepresentable && Doc.Dependencies.Num() == 2 &&
            Doc.Dependencies[0].Field == TEXT("AssetUserData.Key") &&
            Doc.Dependencies[0].Path == TEXT("/Game/SourceUserData.SourceUserData") && !Doc.Dependencies[0].Soft &&
            Doc.Dependencies[1].Field == TEXT("AssetUserData.Value") &&
            Doc.Dependencies[1].Path == TEXT("/Game/SourceSoft.SourceSoft") && Doc.Dependencies[1].Soft &&
            Doc.Dependencies[0].ExpectedClass.IsEmpty() && Doc.Dependencies[1].ExpectedClass.IsEmpty());
    TestTrue(TEXT("Null and empty map sides remain exact serialized values"),
        Doc.ValuesJson.Contains(TEXT("__native_map_pairs")) && Doc.ValuesJson.Contains(TEXT("null")) &&
            Doc.ValuesJson.Contains(TEXT("\"\"")));
    TStrongObjectPtr<UTexture2D> Scratch(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
    RefusesUnchanged(*this, Scratch.Get(), Doc);
    ReportGaps(*this, Doc);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TexturePropertyNestedArrayCoverage,
    "P4MapKit.Quality.TexturePropertyNestedArrayCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TexturePropertyNestedArrayCoverage::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TexturePropertyCoverageNext2;
    auto* NativeArray = CastField<FArrayProperty>(
        UTexture2D::StaticClass()->FindPropertyByName(TEXT("LayerFormatSettings")));
    auto* NativeStruct = NativeArray ? CastField<FStructProperty>(NativeArray->Inner) : nullptr;
    if (!TestTrue(TEXT("Existing real layer array has installed TextureFormatSettings struct"),
            NativeStruct && NativeStruct->Struct->GetName() == TEXT("TextureFormatSettings")))
        return false;
    FP4TexturePropertyNext2SourceFixture Source;
    TArray<uint8> Elements, Payload, Empty;
    {
        FMemoryWriter Element(Elements, true);
        const TArray<uint8> Enum = Source.Named(TEXT("TC_Default"));
        Source.Tag(
            Element, TEXT("CompressionSettings"), TEXT("ByteProperty"), Enum, TEXT("TextureCompressionSettings"));
        Source.Tag(Element, TEXT("OriginalMissingMember"), TEXT("BoolProperty"), Empty, nullptr, nullptr, 0, true);
        Source.Name(Element, TEXT("None"));
    }
    {
        FMemoryWriter Array(Payload, true);
        int32 Count = 1;
        Array << Count;
        Source.Tag(Array, TEXT("LayerFormatSettings"), TEXT("StructProperty"), Elements, TEXT("TextureFormatSettings"));
    }
    Source.Tag(Source.Writer, TEXT("LayerFormatSettings"), TEXT("ArrayProperty"), Payload, TEXT("StructProperty"));
    Source.Finish();
    FP4TexturePropertyDocument Doc;
    FString Error;
    if (!TestTrue(
            TEXT("Complete native outer descriptor and supported tagged children decode"), Source.Inspect(Doc, Error)))
        return false;
    TestTrue(TEXT("Unknown nested member remains exact original data with explicit schema issue"),
        Doc.TaggedPropertiesQualified && !Doc.NativePropertiesRepresentable && Doc.NativePropertyIssues.Num() == 1 &&
            Doc.NativePropertyIssues[0].Contains(TEXT("LayerFormatSettings[0].OriginalMissingMember")) &&
            Doc.ValuesJson.Contains(TEXT("TC_Default")) &&
            Doc.ValuesJson.Contains(TEXT("\"OriginalMissingMember\":true")));
    TStrongObjectPtr<UTexture2D> Scratch(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
    RefusesUnchanged(*this, Scratch.Get(), Doc);
    ReportGaps(*this, Doc);
    return !HasAnyErrors();
}
#endif

#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "P4TextureProperties.h"
#include "Misc/AutomationTest.h"
#include "Curves/CurveVector.h"
#include "Engine/Texture2D.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace P4TexturePropertyAfter201Tests
{
struct FSource
{
    P4MapKit::FPackageInventory Package;
    P4MapKit::FPackageExport Export;
    P4MapKit::FPropertyInventory Properties;
    TArray<uint8> Bytes;
    FMemoryWriter Writer;
    std::vector<std::uint8_t> Body;
    FSource() : Writer(Bytes, true)
    {
        Package.Package = "/Game/P4TexturePropertyAfter201";
        Package.Imports.push_back({"CoreUObject", "Package", "/Script/Engine", 0});
        Package.Imports.push_back({"CoreUObject", "Class", "Texture2D", -1});
        Export.Class = -2;
        Export.Name = "TextureFixture";
    }
    void Name(const TCHAR* Text)
    {
        const std::string Plain = TCHAR_TO_UTF8(Text);
        auto Found = std::find(Package.Names.begin(), Package.Names.end(), Plain);
        int32 Index = int32(Found - Package.Names.begin()), Number = 0;
        if (Found == Package.Names.end())
        {
            Index = int32(Package.Names.size());
            Package.Names.push_back(Plain);
        }
        Writer << Index << Number;
    }
    void Boolean(const TCHAR* Field, int32 ArrayIndex, bool Value)
    {
        Name(Field);
        Name(TEXT("BoolProperty"));
        int32 Size = 0;
        uint8 Bit = Value ? 1 : 0, HasGuid = 0;
        Writer << Size << ArrayIndex << Bit << HasGuid;
    }
    void Finish()
    {
        Name(TEXT("None"));
        Writer.Close();
        Body.assign(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
        Export.Size = Body.size();
        Properties = P4MapKit::ReadPropertyEnvelopes(Body, Package.Names, 0, Body.size());
    }
    bool Inspect(UClass* Schema, FP4TexturePropertyDocument& Doc, FString& Error) const
    {
        return P4InspectTextureProperties(Package, Export, Properties, Body, Schema, Doc, Error);
    }
};
bool Snapshot(UObject* Object, TArray<uint8>& Result)
{
    Result.Empty();
    FObjectWriter Writer(Object, Result, false, false, false, PPF_DuplicateVerbatim);
    if (Writer.IsError() || Result.Num() == 0)
    {
        Result.Empty();
        return false;
    }
    return true;
}
struct FGuard
{
    UTexture2D* Texture;
    TArray<uint8> Before;
    EObjectFlags Flags;
    bool Dirty, SRGB;
    bool Serialized = false;
    FGuid Guid;
    int32 Pre = 0, Post = 0;
    FDelegateHandle A, B;
    explicit FGuard(UTexture2D* In)
        : Texture(In), Flags(In->GetFlags()), Dirty(In->GetOutermost()->IsDirty()), SRGB(In->SRGB),
          Guid(In->GetLightingGuid())
    {
        Serialized = Snapshot(In, Before);
        A = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
            [this](UObject* Object, const FEditPropertyChain&)
            {
                if (Object == Texture)
                    ++Pre;
            });
        B = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
            [this](UObject* Object, FPropertyChangedEvent&)
            {
                if (Object == Texture)
                    ++Post;
            });
    }
    ~FGuard()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(A);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(B);
    }
    bool Ready(FAutomationTestBase& Test) const
    {
        return Test.TestTrue(
            TEXT("Mandatory native baseline serialization succeeds and is nonempty"), Serialized && Before.Num() > 0);
    }
    void Check(FAutomationTestBase& Test) const
    {
        TArray<uint8> After;
        if (!Test.TestTrue(TEXT("Mandatory current native serialization succeeds and is nonempty"),
                Serialized && Before.Num() > 0 && Snapshot(Texture, After) && After.Num() > 0))
            return;
        Test.TestTrue(TEXT("Entire nondelta native texture snapshot retained"), After == Before);
        Test.TestTrue(TEXT("Exact native object flags and prior package dirty state retained"),
            Texture->GetFlags() == Flags && Texture->GetOutermost()->IsDirty() == Dirty);
        Test.TestTrue(TEXT("Independent native boolean and GUID accessors retained"),
            Texture->SRGB == SRGB && Texture->GetLightingGuid() == Guid);
        Test.TestTrue(TEXT("Source inspection/refusal sends no native edit callbacks"), Pre == 0 && Post == 0);
    }
};
void Empty(FAutomationTestBase& Test, const FP4TexturePropertyDocument& Doc)
{
    Test.TestTrue(TEXT("Failure clears complete prior source document"),
        !Doc.TaggedPropertiesQualified && !Doc.NativePropertiesRepresentable && Doc.ClassPath.IsEmpty() &&
            Doc.Package.IsEmpty() && Doc.ValuesJson.IsEmpty() && Doc.ExplicitFields.Num() == 0 &&
            Doc.MissingRuntimeDefaults.Num() == 0 && Doc.MissingEditorSourceSettings.Num() == 0 &&
            Doc.Dependencies.Num() == 0 && Doc.NativePropertyIssues.Num() == 0);
}
void Seed(FP4TexturePropertyDocument& Doc)
{
    Doc.TaggedPropertiesQualified = Doc.NativePropertiesRepresentable = true;
    Doc.ClassPath = TEXT("prior-class");
    Doc.Package = TEXT("prior-package");
    Doc.ValuesJson = TEXT("prior-values");
    Doc.ExplicitFields.Add(TEXT("prior-field"));
    Doc.NativePropertyIssues.Add(TEXT("prior-issue"));
    Doc.MissingRuntimeDefaults.Add(TEXT("prior-runtime"));
    Doc.MissingEditorSourceSettings.Add(TEXT("prior-editor"));
    FP4TexturePropertyDocument::FDependency Ref;
    Ref.Field = TEXT("prior-ref");
    Ref.Path = TEXT("/Game/Prior.Prior");
    Doc.Dependencies.Add(Ref);
}
} //namespace P4TexturePropertyAfter201Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TexturePropertyNonTextureSchemaAfter201,
    "P4MapKit.Quality.TexturePropertyNonTextureSchemaAfter201",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TexturePropertyNonTextureSchemaAfter201::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TexturePropertyAfter201Tests;
    FSource Source;
    Source.Boolean(TEXT("SRGB"), 0, false);
    Source.Finish();
    const auto Body = Source.Body;
    const auto Names = Source.Package.Names;
    FP4TexturePropertyDocument Doc;
    FString Error;
    if (!TestTrue(TEXT("Baseline bool source has valid fresh native envelope"),
            Source.Properties.Error.Code.empty() && Source.Inspect(UTexture2D::StaticClass(), Doc, Error) &&
                Doc.TaggedPropertiesQualified && Doc.NativePropertiesRepresentable))
        return false;
    TestTrue(TEXT("Baseline value remains exact"),
        Doc.ValuesJson.Equals(TEXT("{\"SRGB\":false}"), ESearchCase::CaseSensitive));
    TStrongObjectPtr<UTexture2D> Scratch(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
    Scratch->SRGB = true;
    Scratch->SetLightingGuid(FGuid(0xffffffffu, 0x80000000u, 31, 17));
    FGuard State(Scratch.Get());
    if (!State.Ready(*this))
        return false;
    Seed(Doc);
    TestFalse(TEXT("Genuine existing non-texture class refuses before class/source adaptation"),
        Source.Inspect(UCurveVector::StaticClass(), Doc, Error));
    TestTrue(TEXT("Refusal names actual supported schema gate"),
        Error.Contains(TEXT("supported complete schema"), ESearchCase::CaseSensitive));
    Empty(*this, Doc);
    FString Canonical(TEXT("previous"));
    TestFalse(TEXT("Cleared refused document cannot alter a native private texture"),
        P4PrepareTextureProperties(Scratch.Get(), Doc, Canonical, Error));
    TestTrue(TEXT("Refusal clears canonical output"), Canonical.IsEmpty());
    TestTrue(TEXT("Original serialized source and name table are unchanged"),
        Source.Body == Body && Source.Package.Names == Names);
    State.Check(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TexturePropertyCanonicalKeyCollisionAfter201,
    "P4MapKit.Quality.TexturePropertyCanonicalKeyCollisionAfter201",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TexturePropertyCanonicalKeyCollisionAfter201::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TexturePropertyAfter201Tests;
    // Source envelope identities retain the separate name and array index.
    // A literal source name containing brackets must not overwrite the JSON
    // key derived from another name's serialized index. These are malformed
    // native inputs with valid distinct outer envelopes, not invented schema.
    TStrongObjectPtr<UTexture2D> Scratch(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
    Scratch->SRGB = true;
    Scratch->SetLightingGuid(FGuid(2, 3, 5, 7));
    FGuard State(Scratch.Get());
    if (!State.Ready(*this))
        return false;
    auto* SRGB = CastField<FBoolProperty>(Scratch->GetClass()->FindPropertyByName(TEXT("SRGB")));
    if (!TestNotNull(TEXT("Actual installed scalar boolean property"), SRGB))
        return false;
    const EPropertyFlags PropertyFlags = SRGB->GetPropertyFlags();
    TestEqual(TEXT("Real native scalar dimension remains one"), SRGB->ArrayDim, 1);
    for (int32 Order = 0; Order < 2; ++Order)
    {
        FSource Source;
        if (Order == 0)
        {
            Source.Boolean(TEXT("SRGB"), 1, false);
            Source.Boolean(TEXT("SRGB[1]"), 0, true);
        }
        else
        {
            Source.Boolean(TEXT("SRGB[1]"), 0, true);
            Source.Boolean(TEXT("SRGB"), 1, false);
        }
        Source.Finish();
        const auto Body = Source.Body;
        const auto Names = Source.Package.Names;
        if (!TestTrue(TEXT("Core independently retains both distinct source envelope identities"),
                Source.Properties.Error.Code.empty() && Source.Properties.Properties.size() == 2))
            return false;
        TestTrue(TEXT("Native name and index are distinct despite collision of derived keys"),
            Source.Properties.Properties[0].Name != Source.Properties.Properties[1].Name &&
                Source.Properties.Properties[0].ArrayIndex != Source.Properties.Properties[1].ArrayIndex);
        FP4TexturePropertyDocument Doc;
        Seed(Doc);
        FString Error;
        TestFalse(TEXT("Neither source ordering silently chooses one colliding value"),
            Source.Inspect(UTexture2D::StaticClass(), Doc, Error));
        TestTrue(TEXT("Refusal reaches derived source-key collision gate"),
            Error.Equals(TEXT("SRGB[1]: duplicate original property"), ESearchCase::CaseSensitive));
        Empty(*this, Doc);
        FString Canonical(TEXT("prior"));
        TestFalse(TEXT("Ambiguous source cannot edit a native texture"),
            P4PrepareTextureProperties(Scratch.Get(), Doc, Canonical, Error));
        TestTrue(TEXT("No stale canonical output survives"), Canonical.IsEmpty());
        TestTrue(TEXT("Original two source identities and bytes are not normalized away"),
            Source.Body == Body && Source.Package.Names == Names);
        State.Check(*this);
    }
    FSource Distinct;
    Distinct.Boolean(TEXT("SRGB"), 1, false);
    Distinct.Boolean(TEXT("SRGB[2]"), 0, true);
    Distinct.Finish();
    FP4TexturePropertyDocument Doc;
    FString Error;
    TestTrue(TEXT("Noncolliding complete source values remain inspectable with native issues"),
        Distinct.Inspect(UTexture2D::StaticClass(), Doc, Error) && Doc.TaggedPropertiesQualified &&
            !Doc.NativePropertiesRepresentable && Doc.NativePropertyIssues.Num() == 2 &&
            Doc.ExplicitFields.Contains(TEXT("SRGB[1]")) && Doc.ExplicitFields.Contains(TEXT("SRGB[2]")) &&
            Doc.ValuesJson.Contains(TEXT("\"SRGB[1]\":false"), ESearchCase::CaseSensitive) &&
            Doc.ValuesJson.Contains(TEXT("\"SRGB[2]\":true"), ESearchCase::CaseSensitive));
    TestTrue(TEXT("Real native property flags and scalar dimension remain unchanged"),
        SRGB->GetPropertyFlags() == PropertyFlags && SRGB->ArrayDim == 1);
    State.Check(*this);
    return !HasAnyErrors();
}
#endif

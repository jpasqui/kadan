// Private test-only draft: source reviewed, uncompiled and unexecuted.
#include "P4AuthoringRows.h"
#include "P4NativeValueStaging.h"
#include "P4OriginalMeshNegativeTests.h"
#include "P4NativeHash.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "Engine/DataTable.h"
#include "Curves/CurveVector.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/NoExportTypes.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace P4MeshPreparationCoverage
{
TArray<uint8> Snapshot(UObject* Object)
{
    TArray<uint8> Out;
    FObjectWriter Writer(Object, Out, false, false, false, PPF_DuplicateVerbatim);
    return Out;
}
TSharedPtr<FJsonObject> Parse(const FString& Text)
{
    TSharedPtr<FJsonObject> Out;
    FJsonSerializer::Deserialize(
        TJsonReaderFactory<>::Create(Text), Out, FJsonSerializer::EFlags::StoreNumbersAsStrings);
    return Out;
}
bool Valid(const TSharedPtr<FJsonObject>& Object)
{
    bool Out = false;
    return Object.IsValid() && Object->TryGetBoolField(TEXT("valid"), Out) && Out;
}
FString Quote(const FString& Value)
{
    FString Out;
    auto Writer = TJsonWriterFactory<>::Create(&Out);
    Writer->WriteArrayStart();
    Writer->WriteValue(Value);
    Writer->WriteArrayEnd();
    Writer->Close();
    return Out.Mid(1, Out.Len() - 2);
}
// Transport only: retain every original number token without floating conversion.
FString Exact(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid())
        return FString();
    if (Value->Type == EJson::Number)
    {
        FString Token;
        return Value->TryGetString(Token) ? Token : FString();
    }
    if (Value->Type == EJson::String)
        return Quote(Value->AsString());
    if (Value->Type == EJson::Boolean)
        return Value->AsBool() ? TEXT("true") : TEXT("false");
    if (Value->Type == EJson::Null)
        return TEXT("null");
    TArray<FString> Parts;
    if (Value->Type == EJson::Array)
    {
        for (const auto& Item : Value->AsArray())
            Parts.Add(Exact(Item));
        return TEXT("[") + FString::Join(Parts, TEXT(",")) + TEXT("]");
    }
    if (Value->Type == EJson::Object)
    {
        TArray<FString> Keys;
        Value->AsObject()->Values.GetKeys(Keys);
        Keys.Sort();
        for (const auto& Key : Keys)
            Parts.Add(Quote(Key) + TEXT(":") + Exact(Value->AsObject()->Values[Key]));
        return TEXT("{") + FString::Join(Parts, TEXT(",")) + TEXT("}");
    }
    return FString();
}
FString Document(const TSharedPtr<FJsonObject>& Object)
{
    return Exact(MakeShared<FJsonValueObject>(Object));
}
bool PairUnchanged(const FString& HeaderPath, const TArray<uint8>& Header, const TArray<uint8>& Body)
{
    TArray<uint8> AfterHeader, AfterBody;
    return FFileHelper::LoadFileToArray(AfterHeader, *HeaderPath) &&
        FFileHelper::LoadFileToArray(AfterBody, *FPaths::ChangeExtension(HeaderPath, TEXT("uexp"))) &&
        AfterHeader == Header && AfterBody == Body;
}
} //namespace P4MeshPreparationCoverage

namespace P4MeshPreparationCoverage
{}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalMeshNonMeshExports, "P4MapKit.Quality.OriginalMeshNonMeshExports",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalMeshNonMeshExports::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4MeshPreparationCoverage;
    const FString HeaderFile = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CYLINDER_SOURCE"));
    const FString Package(TEXT("/Game/Carrot4/Objects/Otakara/OtakaraCollision/SM_stg_Cylinder01"));
    const FString Mesh = Package + TEXT(".SM_stg_Cylinder01");
    const FString Digest(TEXT("72b564e120afd4f0b2dd08cb942bb2275635087c1a76026363027d1d8797fb4a"));
    TArray<uint8> Header, Body;
    if (!TestTrue(TEXT("Root supplies exact original Cylinder pair"),
            !HeaderFile.IsEmpty() && FFileHelper::LoadFileToArray(Header, *HeaderFile) &&
                FFileHelper::LoadFileToArray(Body, *FPaths::ChangeExtension(HeaderFile, TEXT("uexp")))))
        return false;
    FString Actual;
    if (!TestTrue(TEXT("Independent declared Cylinder byte sizes and complete digest"),
            Header.Num() == 2835 && Body.Num() == 7643 &&
                P4HashSourceBuffers(Header.GetData(), Header.Num(), Body.GetData(), Body.Num(), Actual) &&
                Actual == Digest))
        return false;
    FP4OriginalMeshSource Control;
    FString Error;
    if (!TestTrue(TEXT("Actual mesh export decodes all source data as positive control"),
            P4DecodeOriginalMesh(HeaderFile, FString(), Mesh, Control, Error) && Control.Digest == Digest))
        return false;
    {
        FP4OriginalMeshSource Named;
        TestTrue(TEXT("Without a name the mesh is the one named after its package"),
            P4DecodeOriginalMesh(HeaderFile, FString(), FString(), Named, Error) && Named.ExportObject == Mesh &&
                Named.Digest == Digest);
    }
    const FP4OriginalMeshSource Before = P4OriginalMeshNegativeTests::OriginalMeshTestSentinel(Control);
    // Independent ue_tagged original source export census: these are existing
    // nested BodySetup/NavCollision exports, not guessed missing identities.
    for (const TCHAR* Suffix : {TEXT(".BodySetup_0"), TEXT(".NavCollision_0")})
    {
        const FString Export = Mesh + Suffix;
        FP4OriginalMeshSource Out = Before;
        TestFalse(TEXT("Existing non-mesh export cannot enter mesh staging"),
            P4DecodeOriginalMesh(HeaderFile, FString(), Export, Out, Error));
        TestTrue(TEXT("Refusal identifies existing export class rather than missing object"),
            Error.Equals(TEXT("original-mesh-export-class-or-identity"), ESearchCase::CaseSensitive));
        TestTrue(TEXT("Class refusal preserves complete caller-owned sentinel"),
            P4OriginalMeshNegativeTests::OriginalMeshTestUnchanged(Out, Before));
        const auto Verdict = Parse(P4StageOriginalMesh(HeaderFile, FString(), Export));
        if (!TestTrue(TEXT("Class refusal wrapper returns a valid diagnostic envelope"), Verdict.IsValid()))
            return false;
        TestFalse(TEXT("Non-mesh stage cannot report success"), Valid(Verdict));
        TestTrue(TEXT("Wrapper says the class boundary"),
            Verdict->GetStringField(TEXT("error"))
                .Equals(TEXT("original-mesh-export-class-or-identity"), ESearchCase::CaseSensitive));
        for (const TCHAR* Flag : {TEXT("private_staging_qualified"), TEXT("geometry_attributes_verified"),
                 TEXT("editable_import_qualified"), TEXT("editable_asset_qualified"),
                 TEXT("original_defaults_qualified"), TEXT("dependencies_qualified"), TEXT("full_asset_qualified")})
        {
            bool Value = true;
            TestTrue(TEXT("Failed class never gains import/admission qualification"),
                Verdict->TryGetBoolField(Flag, Value) && !Value);
        }
        TestTrue(TEXT("Class refusal exposes no partial materials or geometry"),
            Verdict->GetArrayField(TEXT("materials")).Num() == 0 && Verdict->GetArrayField(TEXT("lods")).Num() == 0);
    }
    TestTrue(TEXT("All class refusals preserve original source pair"), PairUnchanged(HeaderFile, Header, Body));
    return !HasAnyErrors();
}
#endif

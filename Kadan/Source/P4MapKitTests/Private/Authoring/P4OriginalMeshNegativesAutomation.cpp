// Native original source contradiction tests; isolated owned fixtures only.
#include "P4OriginalMeshNegativeTests.h"
#include "P4AuthoringRows.h"
#include "P4NativeHash.h"
#if WITH_DEV_AUTOMATION_TESTS && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"

namespace P4OriginalMeshNegativeTests
{
struct FOriginalMeshTestMutation
{
    const TCHAR* Label;
    int32 Offset;
    uint32 Before, After;
    int32 Bytes;
    const TCHAR* Expected;
    bool ErrorPrefix = false;
};
bool OriginalMeshTestJson(const FString& Text, TSharedPtr<FJsonObject>& Out)
{
    return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
}
uint32 OriginalMeshTestRead(const TArray<uint8>& Bytes, int32 At, int32 Count)
{
    uint32 Value = 0;
    for (int32 K = 0; K < Count; ++K)
        Value |= uint32(Bytes[At + K]) << (K * 8);
    return Value;
}
void OriginalMeshTestWrite(TArray<uint8>& Bytes, const FOriginalMeshTestMutation& Mutation)
{
    for (int32 K = 0; K < Mutation.Bytes; ++K)
        Bytes[Mutation.Offset + K] = uint8(Mutation.After >> (K * 8));
}
} //namespace P4OriginalMeshNegativeTests

bool P4TestOriginalMeshMaterialNegatives(
    FAutomationTestBase& Test, const FString& OriginalCylinderHeader, const FString& OwnedPrivateScratchDirectory)
{
    using namespace P4OriginalMeshNegativeTests;
    bool Good = true;
    const auto Check = [&](const FString& Label, bool Value)
    {
        Good = Test.TestTrue(Label, Value) && Good;
        return Value;
    };
    const FString Package = TEXT("/Game/Carrot4/Objects/Otakara/OtakaraCollision/SM_stg_Cylinder01");
    const FString Export = Package + TEXT(".SM_stg_Cylinder01");
    const FString OriginalDigest = TEXT("72b564e120afd4f0b2dd08cb942bb2275635087c1a76026363027d1d8797fb4a");
    const FString OriginalBody = FPaths::ChangeExtension(OriginalCylinderHeader, TEXT("uexp"));
    const FString Scratch = OwnedPrivateScratchDirectory;
    if (!Check(TEXT("Declared source fixture has independent exact original lengths before allocation"),
            IFileManager::Get().FileSize(*OriginalCylinderHeader) == 2835 &&
                IFileManager::Get().FileSize(*OriginalBody) == 7643))
        return false;
    TArray<uint8> Header, Body;
    if (!Check(TEXT("Original fixture can be read without mutation"),
            FFileHelper::LoadFileToArray(Header, *OriginalCylinderHeader) &&
                FFileHelper::LoadFileToArray(Body, *OriginalBody)))
        return false;
    FString Digest;
    if (!Check(TEXT("Fixture is exact independent four-source census Cylinder, not a guessed package"),
            P4HashSourceBuffers(Header.GetData(), Header.Num(), Body.GetData(), Body.Num(), Digest) &&
                Digest == OriginalDigest))
        return false;
    if (!Check(TEXT("One reused private scratch directory is available"),
            IFileManager::Get().MakeDirectory(*Scratch, true)))
        return false;
    const FString HeaderFile = FPaths::Combine(Scratch, TEXT("native-negative-fixture.uasset"));
    const FString BodyFile = FPaths::ChangeExtension(HeaderFile, TEXT("uexp"));
    if (!Check(TEXT("Owned fixture header can be written once"), FFileHelper::SaveArrayToFile(Header, *HeaderFile)))
        return false;
    TArray<uint8> WrittenHeader = Header;

    // Writes a case's header and body to the test's two files and takes the digest of exactly those bytes.
    const auto Capture =
        [&](const TArray<uint8>& ExactBody, FString& ExactDigest, const TArray<uint8>* ExactHeader = nullptr)
    {
        const auto& H = ExactHeader ? *ExactHeader : Header;
        if (H != WrittenHeader)
        {
            if (!Check(TEXT("Changed owned header written only when necessary"),
                    FFileHelper::SaveArrayToFile(H, *HeaderFile)))
                return false;
            WrittenHeader = H;
        }
        return Check(TEXT("Owned body mutation writes only reused private input"),
                   FFileHelper::SaveArrayToFile(ExactBody, *BodyFile)) &&
            Check(TEXT("Fresh exact mutated byte digest computed"),
                P4HashSourceBuffers(H.GetData(), H.Num(), ExactBody.GetData(), ExactBody.Num(), ExactDigest));
    };
    FString ControlDigest;
    if (!Capture(Body, ControlDigest))
        return false;
    FP4OriginalMeshSource Control;
    FString Error;
    if (!Check(TEXT("Unchanged source fully decodes material/reference/UV metadata before negative cases"),
            P4DecodeOriginalMesh(HeaderFile, BodyFile, Export, Control, Error) && Control.Digest == ControlDigest))
        return false;
    Check(TEXT("Source-valid Cylinder control has one material and original source color absence"),
        Control.MaterialsValidated && Control.RenderDecoded && Control.Materials.Num() == 1 &&
            Control.Render.LODs.size() == 1 && Control.Render.LODs[0].Buffers.ColorRGBA.empty());
    const FP4OriginalMeshSource Sentinel = OriginalMeshTestSentinel(Control);
    // Literal offsets/values come from the independent original-material-tags
    // census, not the adapter or decoder implementation. All mutations preserve
    // byte lengths and native render footer bytes to test real contradictions.
    const FOriginalMeshTestMutation Cases[] = {{TEXT("Top field indexed instead of scalar"), 3925, 0, 1, 4,
                                                   TEXT("original-mesh-static-materials-duplicate-or-name-identity")},
        {TEXT("Unknown native StaticMaterial struct GUID"), 3974, 0, 1, 4,
            TEXT("original-mesh-material-array-descriptor")},
        {TEXT("Unknown native UV struct GUID"), 4118, 0, 1, 4, TEXT("original-mesh-uv-struct-schema")},
        {TEXT("Tagged valid object reference contradicts native footer"), 4016, 0xfffffffbu, 0xfffffffcu, 4,
            TEXT("original-mesh-material-object-identity-disagreement")},
        {TEXT("Material object moved into unrepresented fixed-array slot"), 4011, 0, 1, 4,
            TEXT("original-mesh-material-source-field-missing")},
        {TEXT("Representable numbered native name contradicts native footer"), 4049, 0, 1, 4,
            TEXT("original-mesh-material-native-slot-disagreement")},
        {TEXT("Unknown material member with same exact field count"), 4086, 79, 8, 4,
            TEXT("original-mesh-material-source-field-missing")},
        {TEXT("Unknown UV fixed-array index without omitted-value default"), 4207, 0, 4, 4,
            TEXT("original-mesh-uv-density-schema")},
        {TEXT("Tagged initialized bool contradicts native footer"), 4159, 1, 0, 1,
            TEXT("original-mesh-uv-native-bool-disagreement")},
        {TEXT("Tagged override bool contradicts native footer"), 4185, 0, 1, 1,
            TEXT("original-mesh-uv-native-bool-disagreement")},
        {TEXT("Finite tagged float contradicts original native float bits"), 4212, 0x4309d482u, 0x3f800000u, 4,
            TEXT("original-mesh-uv-density-bit-disagreement")},
        {TEXT("Numerically equal signed zero cannot hide precision loss"), 4270, 0, 0x80000000u, 4,
            TEXT("original-mesh-uv-density-bit-disagreement")},
        {TEXT("Negative source name index"), 4045, 44, 0xffffffffu, 4, TEXT("invalid-name@"), true},
        {TEXT("Negative source name number"), 4049, 0, 0xffffffffu, 4, TEXT("invalid-name@"), true},
        {TEXT("Tagged NaN cannot become a finite density default"), 4212, 0x4309d482u, 0x7fc00000u, 4,
            TEXT("nonfinite-property@"), true}};
    for (const auto& Case : Cases)
    {
        if (!Check(Case.Label + FString(TEXT(": independent literal preimage matches")),
                Case.Offset >= 0 && Case.Bytes > 0 && Case.Bytes <= 4 && Case.Offset <= Body.Num() - Case.Bytes &&
                    OriginalMeshTestRead(Body, Case.Offset, Case.Bytes) == Case.Before))
            return false;
        TArray<uint8> Mutation = Body;
        OriginalMeshTestWrite(Mutation, Case);
        FString MutatedDigest;
        if (!Capture(Mutation, MutatedDigest))
            return false;
        if (!Check(TEXT("Actual private mutation has a distinct source binding"), MutatedDigest != ControlDigest))
            return false;
        FP4OriginalMeshSource Out = Sentinel;
        Check(Case.Label + FString(TEXT(": typed parser refuses")),
            !P4DecodeOriginalMesh(HeaderFile, BodyFile, Export, Out, Error));
        Check(Case.Label + FString(TEXT(": intended real parser boundary reached")),
            Case.ErrorPrefix ? Error.StartsWith(Case.Expected) : Error == Case.Expected);
        Check(Case.Label + FString(TEXT(": caller-owned output unchanged")), OriginalMeshTestUnchanged(Out, Sentinel));
        TSharedPtr<FJsonObject> Refusal;
        bool Value = true;
        if (!Check(TEXT("Reflected source wrapper returns inspectable refusal JSON"),
                OriginalMeshTestJson(P4StageOriginalMesh(HeaderFile, BodyFile, Export), Refusal)))
            return false;
        Check(TEXT("Parser refusal has no success qualification"),
            Refusal->TryGetBoolField(TEXT("valid"), Value) && !Value);
        for (const TCHAR* Flag : {TEXT("private_staging_qualified"), TEXT("geometry_attributes_verified"),
                 TEXT("editable_import_qualified"), TEXT("editable_asset_qualified"),
                 TEXT("original_defaults_qualified"), TEXT("dependencies_qualified"), TEXT("full_asset_qualified")})
            Check(FString(TEXT("Refusal keeps flag false: ")) + Flag, Refusal->TryGetBoolField(Flag, Value) && !Value);
        Check(TEXT("Failed decoder returns no partial material/LOD stage hashes"),
            Refusal->GetArrayField(TEXT("materials")).Num() == 0 && Refusal->GetArrayField(TEXT("lods")).Num() == 0);
    }
    // Source-table name 44 is an eight-byte literal at offset 1376 (preceded
    // by FString length 9). Name numbers in the body remain zero; the native
    // footer independently resolves the same changed source-table base.
    TArray<uint8> LiteralSuffixHeader = Header;
    const uint8 OriginalBase[] = {'l', 'a', 'm', 'b', 'e', 'r', 't', '1', 0};
    const uint8 NumericLiteral[] = {'v', 'a', 'l', 'u', 'e', '_', '_', '1', 0};
    if (!Check(TEXT("Numeric literal-base case matches independent source name-table preimage"),
            FMemory::Memcmp(Header.GetData() + 1376, OriginalBase, sizeof(OriginalBase)) == 0))
        return false;
    FMemory::Memcpy(LiteralSuffixHeader.GetData() + 1376, NumericLiteral, sizeof(NumericLiteral));
    FString LiteralDigest;
    if (!Capture(Body, LiteralDigest, &LiteralSuffixHeader))
        return false;
    FP4OriginalMeshSource LiteralOut = Sentinel;
    Check(TEXT("Unsplit original numeric suffix cannot silently change FName identity"),
        !P4DecodeOriginalMesh(HeaderFile, BodyFile, Export, LiteralOut, Error) &&
            Error == TEXT("original-mesh-name-native-representation-loss"));
    Check(TEXT("Numeric FName representation refusal leaves source output unchanged"),
        OriginalMeshTestUnchanged(LiteralOut, Sentinel));
    TArray<uint8> AliasHeader = Header;
    const uint8 ExactTopName[] = {'S', 't', 'a', 't', 'i', 'c', 'M', 'a', 't', 'e', 'r', 'i', 'a', 'l', 's', 0};
    if (!Check(TEXT("Case alias matches independent source top-field name-table preimage"),
            FMemory::Memcmp(Header.GetData() + 2027, ExactTopName, sizeof(ExactTopName)) == 0))
        return false;
    AliasHeader[2027] = uint8('s');
    FString AliasDigest;
    if (!Capture(Body, AliasDigest, &AliasHeader))
        return false;
    FP4OriginalMeshSource AliasOut = Sentinel;
    Check(TEXT("Native case-equivalent source field cannot disappear from material accounting"),
        !P4DecodeOriginalMesh(HeaderFile, BodyFile, Export, AliasOut, Error) &&
            Error == TEXT("original-mesh-static-materials-duplicate-or-name-identity"));
    Check(TEXT("Case alias refusal leaves caller source output unchanged"),
        OriginalMeshTestUnchanged(AliasOut, Sentinel));
    // The exact original again in the test's files, for the cancellation checks.
    if (!Capture(Body, ControlDigest))
        return false;
    Good = P4TestOriginalMeshCancellation(Test, HeaderFile, Export) && Good;
    TArray<uint8> HeaderAfter, BodyAfter;
    Check(TEXT("Foreign original fixture still reads unchanged"),
        FFileHelper::LoadFileToArray(HeaderAfter, *OriginalCylinderHeader) &&
            FFileHelper::LoadFileToArray(BodyAfter, *OriginalBody) && HeaderAfter == Header && BodyAfter == Body);
    return Good;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalMeshSourceNegativesTest, "P4MapKit.Quality.OriginalMeshSourceNegatives",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalMeshSourceNegativesTest::RunTest(const FString&)
{
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CYLINDER_SOURCE"));
    if (!TestTrue(TEXT("Root supplies the exact independently bound Cylinder source"), !Header.IsEmpty()))
        return false;
    const FString Scratch = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("P4NativeQuality/OriginalMeshNegatives"));
    return P4TestOriginalMeshMaterialNegatives(*this, Header, Scratch) && !HasAnyErrors();
}
#endif

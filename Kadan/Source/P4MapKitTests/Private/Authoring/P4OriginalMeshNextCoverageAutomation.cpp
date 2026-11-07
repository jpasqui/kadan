// Private mesh-only regression draft. Not compiled or executed by this worker.
// Mutations are bounded owned copies of the exact original Cylinder pair.
#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4AuthoringRows.h"
#include "P4OriginalMeshStaging.h"
#include "P4OriginalMeshNegativeTests.h"
#include "P4NativeHash.h"
#include "P4OriginalMeshCoverageShared.h"
// PRIVATE, uncompiled/unexecuted fragment. Append after the existing boundary
// tests in P4OriginalMeshBoundaryCoverageAutomation.cpp; reuse its FM89Suite.
// Historical target coordinates are batch110. Current138 tests are subtracted;
// final coverage claims require the fresh138 measurement (root-owned).
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4MeshSemanticNext
{
using namespace P4MeshCoverage89;
struct FMutation
{
    const TCHAR* Label;
    int32 Offset;
    uint32 Before, After;
    const TCHAR* Error;
};
bool OriginalCensus(FM89Suite& F)
{
    if (!F.Check(TEXT("Independent original native census: one LOD,52 vertices,48 triangles and one actual material"),
            F.Control.Render.Start == 4622 && F.Control.Render.End == 7639 && F.Control.Render.LODs.size() == 1 &&
                F.Control.Materials.Num() == 1))
        return false;
    const auto& Buffers = F.Control.Render.LODs[0].Buffers;
    return F.Check(TEXT("Exact independently censused original geometry inventory and color absence"),
        Buffers.Vertices == 52 && Buffers.PositionValues.size() == 52 && Buffers.TangentValues.size() == 52 &&
            Buffers.Main.Indices.size() == 144 && Buffers.ColorRGBA.empty() &&
            F.Control.Materials[0].Slot.ObjectIndex == -5 && F.Control.Materials[0].NativeName.SourceIndex == 44 &&
            F.Control.Materials[0].NativeName.SourceNumber == 0);
}
TArray<uint8> OutputStorage(const FP4OriginalMeshSource& Out)
{
    // Snapshot the SAME initialized sentinel object, not two independently
    // allocated C++ objects. No equality inference from aggregate padding.
    // Include the sentinel's populated nested storage as well as its handles;
    // OriginalMeshTestUnchanged independently checks every populated value.
    TArray<uint8> Bytes;
    const auto Add = [&](const void* Data, SIZE_T Count)
    {
        if (Count)
            Bytes.Append(static_cast<const uint8*>(Data), int32(Count));
    };
    Add(&Out, sizeof(Out));
    Add(Out.Materials.GetData(), SIZE_T(Out.Materials.Num()) * sizeof(FP4OriginalMeshMaterial));
    Add(Out.OtherTaggedProperties.GetData(), SIZE_T(Out.OtherTaggedProperties.Num()) * sizeof(FString));
    Add(Out.Render.LODs.data(), Out.Render.LODs.size() * sizeof(Out.Render.LODs[0]));
    for (const auto& LOD : Out.Render.LODs)
        Add(LOD.Buffers.PositionValues.data(),
            LOD.Buffers.PositionValues.size() * sizeof(LOD.Buffers.PositionValues[0]));
    return Bytes;
}
bool SameNativeMeshes(const TMap<FString, UObject*>& Before)
{
    const auto After = M89NativeMeshes();
    bool Same = Before.Num() == After.Num();
    for (const auto& Pair : Before)
    {
        const auto* Current = After.Find(Pair.Key);
        Same = Same && Current && *Current == Pair.Value;
    }
    return Same;
}
void Refuses(FM89Suite& F, const FM89Case& C, const TCHAR* Label, const TCHAR* Expected)
{
    FP4OriginalMeshSource Out = F.Sentinel;
    const auto Before = OutputStorage(Out);
    const auto Meshes = M89NativeMeshes();
    FString Error(TEXT("stale error"));
    const bool Accepted = P4DecodeOriginalMesh(C.Header, FString(), M89Export, Out, Error);
    F.Trace(Label, Accepted, Error);
    F.Check(FString(Label) + TEXT(": explicit actual native refusal at intended source boundary"),
        !Accepted && Error == Expected);
    F.Check(FString(Label) + TEXT(": all destination sentinel object/nested storage bytes are unchanged"),
        OutputStorage(Out) == Before);
    F.Check(FString(Label) + TEXT(": independent populated sentinel strings/numbers/geometry remain exact"),
        P4OriginalMeshNegativeTests::OriginalMeshTestUnchanged(Out, F.Sentinel));
    // Existing helper independently repeats admission and invokes the reflected
    // wrapper, checking bound digest, zero material/LOD publication and exact
    // captured file bytes. No authentic source input is modified by these calls.
    F.Refuses(Label, C, Expected);
    F.Check(FString(Label) + TEXT(": original StaticMesh paths and pointers are unchanged"), SameNativeMeshes(Meshes));
    F.Check(FString(Label) + TEXT(": the control's files remain as written"), F.Verified(F.ControlCase));
}
bool MutateAndRefuse(FM89Suite& F, const FMutation& Case)
{
    TArray<uint8> Body = F.Body;
    if (!M89Write(F, Body, Case.Offset, Case.Before, Case.After))
        return false;
    F.Check(TEXT("Same-size real source mutation preserves every render footer byte"),
        Body.Num() == F.Body.Num() &&
            FMemory::Memcmp(Body.GetData() + 4622, F.Body.GetData() + 4622, 7639 - 4622) == 0);
    FM89Case Captured;
    if (!F.Capture(F.Header, Body, Captured))
        return false;
    F.Check(TEXT("Native capture binds a genuinely different complete source pair"),
        Captured.Digest != F.ControlCase.Digest);
    Refuses(F, Captured, Case.Label, Case.Error);
    return true;
}
} //namespace P4MeshSemanticNext

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshNextDescriptorSchema, "P4MapKit.Quality.MeshNextDescriptorSchemaRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshNextDescriptorSchema::RunTest(const FString&)
{
    using namespace P4MeshSemanticNext;
    FM89Suite F(*this, TEXT("NextDescriptorSchema"));
    if (!F.Open() || !OriginalCensus(F))
        return false;
    // Literal words independently checked by ue_tagged against the authentic
    // 72b564e1... Cylinder pair. Every replacement is another actual source
    // name-table entry; no invented type/name table or synthetic native schema.
    const FMutation Cases[] = {{TEXT("actual-array-inner-type-replaced-with-existing-NameProperty"), 3929, 76, 55,
                                   TEXT("original-mesh-static-materials-source-missing-or-schema")},
        {TEXT("actual-inner-descriptor-name-replaced-with-existing-mesh-name"), 3942, 74, 71,
            TEXT("original-mesh-material-array-descriptor")},
        {TEXT("actual-StaticMaterial-descriptor-replaced-with-existing-MeshUVChannelInfo"), 3966, 73, 52,
            TEXT("original-mesh-material-array-descriptor")}};
    for (const auto& Case : Cases)
        if (!MutateAndRefuse(F, Case))
            return false;
    // Turn the existing outer GUID flag on and keep the same envelope endpoint:
    // its next16 authentic payload bytes become the unsupported property GUID.
    // No extra bytes/defaults are supplied; source data would be lost if admitted.
    TArray<uint8> GuidBody = F.Body;
    if (!M89Write(F, GuidBody, 3921, 381, 365) ||
        !F.Check(TEXT("Original exact outer property GUID flag preimage"), GuidBody[3937] == 0))
        return false;
    GuidBody[3937] = 1;
    FM89Case Guid;
    if (!F.Capture(F.Header, GuidBody, Guid))
        return false;
    Refuses(F, Guid, TEXT("actual-outer-property-GUID-cannot-be-dropped"),
        TEXT("original-mesh-static-materials-source-missing-or-schema"));
    F.Final();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshNextMemberSchema, "P4MapKit.Quality.MeshNextMemberSchemaRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshNextMemberSchema::RunTest(const FString&)
{
    using namespace P4MeshSemanticNext;
    FM89Suite F(*this, TEXT("NextMemberSchema"));
    if (!F.Open() || !OriginalCensus(F))
        return false;
    const FMutation Cases[] = {{TEXT("imported-slot-tag-moved-to-unrepresented-array-index"), 4073, 0, 1,
                                   TEXT("original-mesh-material-source-field-missing")},
        {TEXT("UV-tag-moved-to-unrepresented-array-index"), 4106, 0, 1,
            TEXT("original-mesh-material-source-field-missing")},
        {TEXT("UV-native-struct-identity-replaced-with-existing-StaticMaterial"), 4110, 52, 73,
            TEXT("original-mesh-uv-struct-schema")},
        {TEXT("initialized-member-renamed-without-fabricated-native-bool-default"), 4135, 8, 71,
            TEXT("original-mesh-uv-bool-source-disagreement")},
        {TEXT("float-density-tag-replaced-with-existing-integer-property-type"), 4195, 38, 41,
            TEXT("original-mesh-uv-density-schema")},
        // These prove earlier exact-end guards, not the unreachable adapter
        // line216. Both premature terminators leave real trailing source bytes.
        {TEXT("UV-tagged-record-premature-None-retains-unread-source-bytes"), 4135, 8, 58,
            TEXT("unread-struct-payload@4143")},
        {TEXT("material-record-premature-None-retains-unread-array-elements"), 3991, 50, 58,
            TEXT("struct-array-count-mismatch@3999")}};
    for (const auto& Case : Cases)
        if (!MutateAndRefuse(F, Case))
            return false;
    F.Final();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshNextSourceNameScalars, "P4MapKit.Quality.MeshNextSourceNameScalarBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshNextSourceNameScalars::RunTest(const FString&)
{
    using namespace P4MeshSemanticNext;
    FM89Suite F(*this, TEXT("NextSourceNameScalars"));
    if (!F.Open() || !OriginalCensus(F))
        return false;
    const uint8 Original[] = {'l', 'a', 'm', 'b', 'e', 'r', 't', '1', 0};
    // U+FFFE is valid UTF8 and passes the original core's scalar validator.
    // Installed UE StringConv cannot preserve this source scalar exactly. The
    // original name-table entry length and every native footer byte stay intact.
    const uint8 Noncharacter[] = {0xef, 0xbf, 0xbe, 't', 'e', 's', 't', '1', 0};
    if (!F.Check(TEXT("Independent exact original source-name byte preimage"),
            FMemory::Memcmp(F.Header.GetData() + 1376, Original, sizeof(Original)) == 0))
        return false;
    TArray<uint8> NameHeader = F.Header;
    FMemory::Memcpy(NameHeader.GetData() + 1376, Noncharacter, sizeof(Noncharacter));
    FM89Case Name;
    if (!F.Capture(NameHeader, F.Body, Name))
        return false;
    Refuses(F, Name, TEXT("valid-source-scalar-native-name-cannot-be-lossily-replaced"),
        TEXT("original-mesh-name-source-identity-invalid"));
    // Positive control: the actual imported slot name has a separate source
    // FName number from its native slot. Retain that original identity rather
    // than reject every changed source name or substitute the native footer.
    TArray<uint8> ImportedBody = F.Body;
    if (!M89Write(F, ImportedBody, 4082, 0, 1))
        return false;
    FM89Case Imported;
    if (!F.Capture(F.Header, ImportedBody, Imported))
        return false;
    const auto Meshes = M89NativeMeshes();
    FP4OriginalMeshSource Out;
    FString Error;
    const bool Accepted = P4DecodeOriginalMesh(Imported.Header, FString(), M89Export, Out, Error);
    F.Trace(TEXT("actual-imported-name-number-retained-independently-of-native-slot"), Accepted, Error);
    F.Check(TEXT("Representable actual imported FName identity admits exact source decode"),
        Accepted && Out.Materials.Num() == 1 && Out.Materials[0].ImportedName.SourceIndex == 44 &&
            Out.Materials[0].ImportedName.SourceNumber == 1 &&
            Out.Materials[0].ImportedName.SourceBase == TEXT("lambert1") &&
            Out.Materials[0].ImportedName.Display == TEXT("lambert1_0") &&
            Out.Materials[0].Slot.ImportedSlot == TEXT("lambert1_0") && Out.Materials[0].NativeName.SourceNumber == 0 &&
            Out.Materials[0].Slot.NativeSlot == TEXT("lambert1") && M89GeometryBits(Out, F.Control));
    F.Check(TEXT("Exact positive source control publishes no native mesh or replacement"), SameNativeMeshes(Meshes));
    Imported.Same(F);
    F.Final();
    return F.Good && !HasAnyErrors();
}
#endif

// PRIVATE, uncompiled/unexecuted. Append to the existing original-mesh boundary
// automation CPP after its SemanticNext tests; no helper or fixture duplication.
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4MeshExtentNext
{
using namespace P4MeshCoverage89;
bool ExtentNextOpen(FM89Suite& F)
{
    return F.Open() && P4MeshSemanticNext::OriginalCensus(F) &&
        F.Check(TEXT("Authentic Cylinder geometry is inlined; tagged deletions do not invent bulk offsets"),
            F.Control.Render.LODs[0].Inlined && F.Control.Render.InlinedLODs == 1);
}
bool ExtentNextDelete(FM89Suite& F, TArray<uint8>& Header, TArray<uint8>& Body, int32 At, int32 Count)
{
    if (!F.Check(TEXT("Deletion is confined to a genuine tagged material record"),
            At >= 3991 && Count > 0 && At <= 4319 - Count) ||
        !M89Write(F, Header, 2699, 3734, uint32(3734 - Count)) ||
        !F.Check(TEXT("Actual third export size upper word remains zero"), M89U32(Header, 2703) == 0) ||
        !M89Write(F, Body, 3921, 381, uint32(381 - Count)) || !M89Write(F, Body, 3958, 328, uint32(328 - Count)))
        return false;
    Body.RemoveAt(At, Count, false);
    return F.Check(TEXT("Exact entire source suffix including every native render byte is retained after relocation"),
        Body.Num() == F.Body.Num() - Count && Header.Num() == F.Header.Num() &&
            FMemory::Memcmp(Body.GetData() + At, F.Body.GetData() + At + Count, F.Body.Num() - At - Count) == 0 &&
            FMemory::Memcmp(Body.GetData() + 4622 - Count, F.Body.GetData() + 4622, 7639 - 4622) == 0);
}
bool ExtentNextCapturedRefusal(
    FM89Suite& F, const TArray<uint8>& Header, const TArray<uint8>& Body, const TCHAR* Label, const TCHAR* Error)
{
    FM89Case C;
    if (!F.Capture(Header, Body, C) ||
        !F.Check(TEXT("Actual owned source change has a fresh distinct digest"), C.Digest != F.ControlCase.Digest))
        return false;
    // Existing strong oracle snapshots the same initialized output/storage,
    // checks the native mesh pointers, then independently invokes
    // the public wrapper and requires zero partial material/LOD publication.
    P4MeshSemanticNext::Refuses(F, C, Label, Error);
    return true;
}
} //namespace P4MeshExtentNext

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshNextTaggedMaterialExtents, "P4MapKit.Quality.MeshNextTaggedMaterialExtents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshNextTaggedMaterialExtents::RunTest(const FString&)
{
    using namespace P4MeshExtentNext;
    FM89Suite F(*this, TEXT("NextTaggedMaterialExtents"));
    if (!ExtentNextOpen(F))
        return false;
    // Remove the exact sole tagged element while retaining the genuine native
    // material, section binding and every native render byte. An empty tagged
    // array must not erase source/native disagreement or fabricate a material.
    TArray<uint8> EmptyHeader = F.Header, EmptyBody = F.Body;
    if (!M89Write(F, EmptyBody, 3938, 1, 0) || !ExtentNextDelete(F, EmptyHeader, EmptyBody, 3991, 328) ||
        !ExtentNextCapturedRefusal(F, EmptyHeader, EmptyBody,
            TEXT("empty-tagged-materials-cannot-hide-one-native-material"),
            TEXT("original-mesh-decoded-material-count")))
        return false;
    // Delete the authentic ImportedMaterialSlotName record (33 bytes), not a
    // synthetic missing-value marker. Count=1 and all other actual members stay.
    TArray<uint8> MissingHeader = F.Header, MissingBody = F.Body;
    if (!F.Check(TEXT("Independent actual imported-name record preimage"),
            M89U32(MissingBody, 4053) == 39 && M89U32(MissingBody, 4061) == 55 && M89U32(MissingBody, 4069) == 8 &&
                M89U32(MissingBody, 4073) == 0 && M89U32(MissingBody, 4078) == 44 && M89U32(MissingBody, 4082) == 0) ||
        !ExtentNextDelete(F, MissingHeader, MissingBody, 4053, 33) ||
        !ExtentNextCapturedRefusal(F, MissingHeader, MissingBody,
            TEXT("three-genuine-material-members-cannot-gain-a-default-imported-slot"),
            TEXT("original-mesh-material-fields-unrepresented")))
        return false;
    // Delete the fourth real UV density record (29 bytes) while retaining its
    // genuine native counterpart. Both nested and containing extents are exact.
    TArray<uint8> UVHeader = F.Header, UVBody = F.Body;
    if (!F.Check(TEXT("Independent actual last fixed-array density record preimage"),
            M89U32(UVBody, 4274) == 48 && M89U32(UVBody, 4282) == 38 && M89U32(UVBody, 4290) == 4 &&
                M89U32(UVBody, 4294) == 3 && M89U32(UVBody, 4299) == 0) ||
        !M89Write(F, UVBody, 4102, 176, 147) || !ExtentNextDelete(F, UVHeader, UVBody, 4274, 29) ||
        !ExtentNextCapturedRefusal(F, UVHeader, UVBody,
            TEXT("five-real-UV-members-cannot-fabricate-the-fourth-native-density"),
            TEXT("original-mesh-uv-struct-schema")))
        return false;
    F.Final();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshNextNativeFieldKinds, "P4MapKit.Quality.MeshNextNativeFieldKinds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshNextNativeFieldKinds::RunTest(const FString&)
{
    using namespace P4MeshExtentNext;
    FM89Suite F(*this, TEXT("NextNativeFieldKinds"));
    if (!ExtentNextOpen(F))
        return false;
    const P4MeshSemanticNext::FMutation Cases[] = {
        {TEXT("real-material-reference-tag-corrupted-to-existing-IntProperty"), 3999, 59, 41,
            TEXT("original-mesh-material-source-field-missing")},
        {TEXT("real-native-slot-tag-moved-to-unrepresented-fixed-array-index"), 4040, 0, 1,
            TEXT("original-mesh-material-source-field-missing")}};
    for (const auto& Case : Cases)
        if (!P4MeshSemanticNext::MutateAndRefuse(F, Case))
            return false;
    // The actual imported NameProperty payload is eight bytes. Corrupt its
    // source type to the existing FloatProperty name and remove only its FName
    // number word, leaving an exactly consumable four-byte scalar. Native slot
    // decoding succeeds first; imported identity must then refuse its wrong kind.
    TArray<uint8> Header = F.Header, Body = F.Body;
    if (!M89Write(F, Body, 4061, 55, 38) || !M89Write(F, Body, 4069, 8, 4) ||
        !F.Check(TEXT("Actual removed imported-name number word is zero"), M89U32(Body, 4082) == 0) ||
        !ExtentNextDelete(F, Header, Body, 4082, 4) ||
        !ExtentNextCapturedRefusal(F, Header, Body,
            TEXT("consumable-wrong-imported-name-kind-cannot-qualify-a-source-name"),
            TEXT("original-mesh-name-field-schema")))
        return false;
    F.Final();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshNextDuplicateExportIdentity, "P4MapKit.Quality.MeshNextDuplicateExportIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshNextDuplicateExportIdentity::RunTest(const FString&)
{
    using namespace P4MeshExtentNext;
    FM89Suite F(*this, TEXT("NextDuplicateExportIdentity"));
    if (!ExtentNextOpen(F))
        return false;
    // Existing actual export table starts at2463. Give its first record the
    // existing third record's class/outer/name/number; the two authentic rows
    // then claim the same object identity. No new name, class or table is made.
    TArray<uint8> Header = F.Header;
    if (!F.Check(TEXT("Independent real mesh export identity preimage"),
            M89U32(Header, 2671) == 0xfffffffdu && M89U32(Header, 2683) == 0 && M89U32(Header, 2687) == 71 &&
                M89U32(Header, 2691) == 0) ||
        !M89Write(F, Header, 2463, 0xfffffffeu, 0xfffffffdu) || !M89Write(F, Header, 2475, 3, 0) ||
        !M89Write(F, Header, 2479, 11, 71) || !M89Write(F, Header, 2483, 1, 0) ||
        !ExtentNextCapturedRefusal(F, Header, F.Body,
            TEXT("two-authentic-export-rows-cannot-publish-one-ambiguous-mesh-identity"),
            TEXT("original-mesh-export-class-or-identity")))
        return false;
    F.Final();
    return F.Good && !HasAnyErrors();
}
#endif

// PRIVATE, uncompiled/unexecuted. Append after current ExtentNext boundary
// tests. Reuses the authentic Cylinder fixture and strong sentinel/native
// object/public-wrapper refusal oracles; no production hook is needed.
// Exact163 targets:167:17,164:13,125:9,135:29. Fresh201 decides integration.
// Subfield HasGuid guards93:74/135:69/143:75/200:129 are shadowed by
// FPropertyDecoder::Property's property-guid-requires-adapter refusal. Scalar
// size/read/decoded-kind contradictions are likewise enforced by that decoder;
// no private inventory, fake schema, SHA failure or private stage is injected.
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4MeshSourceNext
{
using namespace P4MeshCoverage89;
using namespace P4MeshExtentNext;
bool SourceNextReplace(FM89Suite& F, TArray<uint8>& Header, TArray<uint8>& Body, int32 At, int32 BeforeCount,
    const TArray<uint8>& Record, bool NestedUV)
{
    const int32 Delta = Record.Num() - BeforeCount;
    if (!F.Check(TEXT("Only a genuine source material/UV tagged record is replaced"),
            At >= 4086 && BeforeCount > 0 && At <= 4319 - BeforeCount && Record.Num() >= 25) ||
        !M89Write(F, Header, 2699, 3734, uint32(3734 + Delta)) ||
        !F.Check(TEXT("Exact real export size high word is retained"), M89U32(Header, 2703) == 0) ||
        !M89Write(F, Body, 3921, 381, uint32(381 + Delta)) || !M89Write(F, Body, 3958, 328, uint32(328 + Delta)))
        return false;
    if (NestedUV && !M89Write(F, Body, 4102, 176, uint32(176 + Delta)))
        return false;
    Body.RemoveAt(At, BeforeCount, false);
    Body.Insert(Record, At);
    return F.Check(TEXT("Every actual following source byte and native render byte survives record relocation exactly"),
        Header.Num() == F.Header.Num() && Body.Num() == F.Body.Num() + Delta &&
            FMemory::Memcmp(Body.GetData() + At + Record.Num(), F.Body.GetData() + At + BeforeCount,
                F.Body.Num() - At - BeforeCount) == 0 &&
            FMemory::Memcmp(Body.GetData() + 4622 + Delta, F.Body.GetData() + 4622, 7639 - 4622) == 0);
}
} //namespace P4MeshSourceNext

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshNextMaterialNamespace, "P4MapKit.Quality.MeshNextMaterialNamespaceRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshNextMaterialNamespace::RunTest(const FString&)
{
    using namespace P4MeshSourceNext;
    FM89Suite F(*this, TEXT("NextMaterialNamespace"));
    if (!ExtentNextOpen(F))
        return false;
    // Rename the actual LightmapUVDensity tag using the already-present source
    // StaticMaterials name and index1. The original index0 tag stays intact;
    // parser identities differ, but case-insensitive material accounting sees
    // two real matching tags and must refuse before selecting one silently.
    TArray<uint8> DuplicateBody = F.Body;
    if (!F.Check(TEXT("Independent actual scalar top-tag preimage"),
            M89U32(DuplicateBody, 4319) == 47 && M89U32(DuplicateBody, 4327) == 38 &&
                M89U32(DuplicateBody, 4335) == 4) ||
        !M89Write(F, DuplicateBody, 4319, 47, 74) || !M89Write(F, DuplicateBody, 4339, 0, 1) ||
        !ExtentNextCapturedRefusal(F, F.Header, DuplicateBody,
            TEXT("second-existing-material-namespace-tag-cannot-be-ignored"),
            TEXT("original-mesh-static-materials-duplicate-or-name-identity")))
        return false;
    // Actual source name47 is the seventeen-byte LightmapUVDensity literal.
    // Replace its first three ASCII bytes with valid UTF8 U+FFFE, retaining its
    // exact source FString byte count. UE's conversion must not replace that
    // scalar silently. This targets top tagged property names, not slot names.
    const uint8 Original[] = {'L', 'i', 'g', 'h', 't', 'm', 'a', 'p', 'U', 'V', 'D', 'e', 'n', 's', 'i', 't', 'y', 0};
    TArray<uint8> Header = F.Header;
    if (!F.Check(TEXT("Independent exact top-property source-name string length and byte preimage"),
            M89U32(Header, 1448) == sizeof(Original) &&
                FMemory::Memcmp(Header.GetData() + 1452, Original, sizeof(Original)) == 0))
        return false;
    Header[1452] = 0xef;
    Header[1453] = 0xbf;
    Header[1454] = 0xbe;
    if (!ExtentNextCapturedRefusal(F, Header, F.Body,
            TEXT("actual-top-property-name-scalar-cannot-be-lossily-replaced"),
            TEXT("original-mesh-property-name-invalid")))
        return false;
    F.Final();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshNextNativeUVFieldShapes, "P4MapKit.Quality.MeshNextNativeUVFieldShapes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshNextNativeUVFieldShapes::RunTest(const FString&)
{
    using namespace P4MeshSourceNext;
    FM89Suite F(*this, TEXT("NextNativeUVFieldShapes"));
    if (!ExtentNextOpen(F))
        return false;
    // Replace only the actual225-byte UV struct record with a consumable float
    // record made from its existing field identity/index, actual GUID flag and
    // authentic first density bits. Existing FloatProperty is source name38.
    TArray<uint8> UVRecord;
    UVRecord.Append(F.Body.GetData() + 4086, 24);
    if (!F.Check(TEXT("Actual UV struct record/type/size/flag preimage"),
            M89U32(UVRecord, 8) == 76 && M89U32(UVRecord, 16) == 176 && M89U32(UVRecord, 20) == 0 &&
                F.Body[4134] == 0) ||
        !M89Write(F, UVRecord, 8, 76, 38) || !M89Write(F, UVRecord, 16, 176, 4))
        return false;
    UVRecord.Add(F.Body[4134]);
    UVRecord.Append(F.Body.GetData() + 4212, 4);
    TArray<uint8> UVHeader = F.Header, UVBody = F.Body;
    if (!SourceNextReplace(F, UVHeader, UVBody, 4086, 225, UVRecord, false) ||
        !ExtentNextCapturedRefusal(F, UVHeader, UVBody,
            TEXT("real-UV-identity-with-consumable-float-kind-cannot-replace-native-struct"),
            TEXT("original-mesh-uv-struct-schema")))
        return false;
    // The actual initialized BoolProperty record is26 bytes. Use its existing
    // name/index, existing IntProperty source name41, exact original GUID flag,
    // and the actual four zero bytes from the third native-tagged UV density.
    // No unsupported bool-size envelope is constructed: the core can consume
    // this integer, and the adapter must refuse its wrong native bool type.
    TArray<uint8> BoolRecord;
    BoolRecord.Append(F.Body.GetData() + 4135, 24);
    if (!F.Check(TEXT("Actual initialized bool record/value/GUID preimage"),
            M89U32(BoolRecord, 0) == 8 && M89U32(BoolRecord, 8) == 12 && M89U32(BoolRecord, 16) == 0 &&
                M89U32(BoolRecord, 20) == 0 && F.Body[4159] == 1 && F.Body[4160] == 0 && M89U32(F.Body, 4270) == 0) ||
        !M89Write(F, BoolRecord, 8, 12, 41) || !M89Write(F, BoolRecord, 16, 0, 4))
        return false;
    BoolRecord.Add(F.Body[4160]);
    BoolRecord.Append(F.Body.GetData() + 4270, 4);
    TArray<uint8> BoolHeader = F.Header, BoolBody = F.Body;
    if (!SourceNextReplace(F, BoolHeader, BoolBody, 4135, 26, BoolRecord, true) ||
        !ExtentNextCapturedRefusal(F, BoolHeader, BoolBody,
            TEXT("real-initialized-field-with-integer-kind-cannot-fabricate-native-bool"),
            TEXT("original-mesh-uv-bool-source-disagreement")))
        return false;
    F.Final();
    return F.Good && !HasAnyErrors();
}
#endif

// Private, uncompiled/unexecuted. Append to P4OriginalMeshBoundaryCoverageAutomation.cpp.
// Reuses its full-file/native inventory helpers. Root supplies explicit
// P4_NATIVE_SHIZAI_SOURCE bound to the existing authentic source census and exact pair hash.
// Exact full201 new target: OriginalMeshStaging.cpp 356:13 true (source vertex absent from polygons).
// No source buffer, material schema, precision flag or color is invented.
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4MeshAfter242
{
using namespace P4MeshCoverage89;
const TCHAR* const After242Package = TEXT(
    "/Game/Carrot4/Objects/WorkObjects/Shizai/SM_stg_ShizaiBF_01/SM_stg_ShizaiBF01");
const TCHAR* const After242Export = TEXT(
    "/Game/Carrot4/Objects/WorkObjects/Shizai/SM_stg_ShizaiBF_01/SM_stg_ShizaiBF01.SM_stg_ShizaiBF01");
const TCHAR* const After242Digest = TEXT("89e56c22c3ce87d01038abb461aa3c398a027825b05127d93d17b69f99d7b17b");
uint32 After242Index(const TArray<uint8>& Body, int32 Index)
{
    const int32 At = 12360 + 2 * Index;
    return uint32(Body[At]) | (uint32(Body[At + 1]) << 8);
}
bool After242SourceAttributesEqual(const FP4OriginalMeshSource& A, const FP4OriginalMeshSource& B)
{
    if (A.Render.LODs.size() != 1 || B.Render.LODs.size() != 1 || A.Materials.Num() != 1 || B.Materials.Num() != 1)
        return false;
    const auto& X = A.Render.LODs[0].Buffers;
    const auto& Y = B.Render.LODs[0].Buffers;
    if (X.Vertices != Y.Vertices || X.TexCoords != Y.TexCoords || X.ColorVertices != Y.ColorVertices ||
        X.HighPrecisionTangents != Y.HighPrecisionTangents || X.FullPrecisionUV != Y.FullPrecisionUV ||
        !M89VectorBits(X.PositionValues, Y.PositionValues) || !M89VectorBits(X.TangentValues, Y.TangentValues) ||
        !M89VectorBits(X.ColorRGBA, Y.ColorRGBA) || X.UVValues.size() != Y.UVValues.size())
        return false;
    for (size_t U = 0; U < X.UVValues.size(); ++U)
        if (!M89VectorBits(X.UVValues[U], Y.UVValues[U]))
            return false;
    const auto& M = A.Materials[0];
    const auto& N = B.Materials[0];
    return M.Slot.ObjectIndex == N.Slot.ObjectIndex && M.Slot.NativeSlot == N.Slot.NativeSlot &&
        M.Slot.ImportedSlot == N.Slot.ImportedSlot && M.NativeName.SourceIndex == N.NativeName.SourceIndex &&
        M.NativeName.SourceNumber == N.NativeName.SourceNumber &&
        M.ImportedName.SourceIndex == N.ImportedName.SourceIndex &&
        M.ImportedName.SourceNumber == N.ImportedName.SourceNumber && M.UVInitialized == N.UVInitialized &&
        M.UVOverride == N.UVOverride && FMemory::Memcmp(M.UVDensities.data(), N.UVDensities.data(), 16) == 0;
}
} //namespace P4MeshAfter242
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshAfter242UnusedSourceVertex, "P4MapKit.Quality.MeshAfter242UnusedSourceVertex",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshAfter242UnusedSourceVertex::RunTest(const FString&)
{
    using namespace P4MeshAfter242;
    FM89Suite F(*this, TEXT("After242UnusedSourceVertex"));
    F.OriginalHeader = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SHIZAI_SOURCE"));
    F.OriginalBody = FPaths::ChangeExtension(F.OriginalHeader, TEXT("uexp"));
    FString Digest, Error;
    if (!F.Check(TEXT("Explicit existing authentic colored source lengths are independently bound"),
            !F.OriginalHeader.IsEmpty() && IFileManager::Get().FileSize(*F.OriginalHeader) == 3249 &&
                IFileManager::Get().FileSize(*F.OriginalBody) == 14523) ||
        !F.Check(TEXT("Existing original source is read without mutation"),
            FFileHelper::LoadFileToArray(F.Header, *F.OriginalHeader) &&
                FFileHelper::LoadFileToArray(F.Body, *F.OriginalBody)) ||
        !F.Check(TEXT("Independent exact authentic source digest agrees"),
            P4HashSourceBuffers(F.Header.GetData(), F.Header.Num(), F.Body.GetData(), F.Body.Num(), Digest) &&
                Digest == After242Digest) ||
        !F.Capture(F.Header, F.Body, F.ControlCase) ||
        !F.Check(TEXT("Actual native baseline material/source decoder succeeds"),
            P4DecodeOriginalMesh(F.ControlCase.Header, FString(), After242Export, F.Control, Error)))
        return false;
    if (!F.Check(TEXT("Native source exposes exactly the independently expected one LOD"),
            F.Control.Render.LODs.size() == 1))
        return false;
    const auto& Original = F.Control.Render.LODs[0];
    if (!F.Check(
            TEXT(
                "Independent retained reference census: one inlined LOD,68 vertices,80 triangles,three UVs and68 actual colors"),
            F.Control.Render.LODs.size() == 1 && F.Control.MaterialsValidated && F.Control.RenderDecoded &&
                Original.Inlined && Original.Buffers.Vertices == 68 && Original.Buffers.ColorVertices == 68 &&
                Original.Buffers.ColorRGBA.size() == 68 && Original.Buffers.TexCoords == 3 &&
                Original.Buffers.Main.Indices.size() == 240 && !Original.Buffers.Main.Wide &&
                Original.Buffers.Main.Data.Span.Source == P4MapKit::EBulkSource::Body &&
                Original.Buffers.Main.Data.Span.Offset == 12360 && Original.Buffers.Main.Data.Span.Size == 480 &&
                Original.Sections.size() == 1 && Original.Sections[0].FirstIndex == 0 &&
                Original.Sections[0].Triangles == 80 && Original.Sections[0].MinVertex == 0 &&
                Original.Sections[0].MaxVertex == 67 && Original.Sections[0].MaterialIndex == 0))
        return false;
    TSet<uint32> OriginalUsed;
    int32 Occurrences = 0;
    for (int32 I = 0; I < 240; ++I)
    {
        const uint32 Value = After242Index(F.Body, I);
        OriginalUsed.Add(Value);
        if (!F.Check(TEXT("Every decoded original index agrees with independent literal native16-bit source bytes"),
                Value == Original.Buffers.Main.Indices[size_t(I)]))
            return false;
        if (Value == 0)
        {
            ++Occurrences;
            if (!F.Check(
                    TEXT("Source vertex0 occurs only at independently observed main corners0 and5"), I == 0 || I == 5))
                return false;
        }
    }
    if (!F.Check(TEXT("Original source uses all68 vertices with precisely two occurrences of vertex0"),
            OriginalUsed.Num() == 68 && Occurrences == 2))
        return false;
    for (int32 I = 0; I < 240; I += 3)
    {
        const uint32 A = After242Index(F.Body, I), B = After242Index(F.Body, I + 1), C = After242Index(F.Body, I + 2);
        if ((A == 0 || B == 0 || C == 0) &&
            !F.Check(TEXT("Existing source vertex4 is nonadjacent to vertex0: remap cannot introduce repeated corners"),
                A != 4 && B != 4 && C != 4))
            return false;
    }
    const auto Baseline = M89Parse(P4StageOriginalMesh(F.ControlCase.Header, FString(), After242Export));
    if (!F.Check(
            TEXT("Unmodified authentic colored source independently completes actual private MeshDescription readback"),
            Baseline.IsValid() && Baseline->GetBoolField(TEXT("valid")) &&
                Baseline->GetArrayField(TEXT("lods")).Num() == 1))
        return false;
    TArray<uint8> Mutated = F.Body;
    Mutated[12360] = 4;
    Mutated[12370] = 4;
    int32 Changed = 0;
    for (int32 I = 0; I < Mutated.Num(); ++I)
        if (Mutated[I] != F.Body[I])
        {
            ++Changed;
            if (!F.Check(TEXT("Only the two independently observed low source index bytes change"),
                    I == 12360 || I == 12370))
                return false;
        }
    if (!F.Check(
            TEXT(
                "Exactly two main index bytes change; all positions/colors/tangents/UVs/auxiliary buffers/metadata survive byte-exact"),
            Changed == 2))
        return false;
    FM89Case C;
    if (!F.Capture(F.Header, Mutated, C) ||
        !F.Check(TEXT("Mutated source has a fresh distinct digest"), C.Digest != F.ControlCase.Digest))
        return false;
    FP4OriginalMeshSource Decoded;
    if (!F.Check(TEXT("Real native source decoder accepts the bounded original index remap"),
            P4DecodeOriginalMesh(C.Header, FString(), After242Export, Decoded, Error)) ||
        !F.Check(TEXT("Every retained vertex attribute/material/UV metadata bit remains exact"),
            After242SourceAttributesEqual(Decoded, F.Control)))
        return false;
    TSet<uint32> Used;
    TArray<uint8> IndependentIndices;
    for (int32 I = 0; I < 240; ++I)
    {
        const uint32 Value = After242Index(Mutated, I);
        Used.Add(Value);
        if (!F.Check(TEXT("Every new decoded index agrees with independently mutated source16-bit payload"),
                Decoded.Render.LODs[0].Buffers.Main.Indices[size_t(I)] == Value))
            return false;
        for (int32 Byte = 0; Byte < 4; ++Byte)
            IndependentIndices.Add(uint8(Value >> (8 * Byte)));
    }
    if (!F.Check(TEXT("Exactly one original source vertex is now unused, still retaining its actual native attributes"),
            Used.Num() == 67 && !Used.Contains(0) && Used.Contains(4) &&
                Decoded.Render.LODs[0].Buffers.PositionValues.size() == 68))
        return false;
    FString IndexDigest;
    if (!F.Check(TEXT("Independent source-oriented32-bit index fingerprint succeeds"),
            P4HashByteStream([&](const TFunction<bool(const uint8*, int32)>& Update)
                { return Update(IndependentIndices.GetData(), IndependentIndices.Num()); }, IndexDigest)))
        return false;
    const auto Result = M89Parse(P4StageOriginalMesh(C.Header, FString(), After242Export));
    if (!F.Check(TEXT("Actual native wrapper preserves unused source attributes and completes all hashes"),
            Result.IsValid() && Result->GetBoolField(TEXT("valid")) &&
                Result->GetBoolField(TEXT("geometry_attributes_verified")) &&
                Result->GetArrayField(TEXT("lods")).Num() == 1))
        return false;
    const auto Before = Baseline->GetArrayField(TEXT("lods"))[0]->AsObject();
    const auto After = Result->GetArrayField(TEXT("lods"))[0]->AsObject();
    F.Check(TEXT("Unused source readback retains complete68-vertex/240-corner/3-channel census"),
        After->GetNumberField(TEXT("vertices")) == 68 && After->GetNumberField(TEXT("corners")) == 240 &&
            After->GetNumberField(TEXT("unused_vertices")) == 1 && After->GetNumberField(TEXT("uv_channels")) == 3);
    for (const TCHAR* Hash :
        {TEXT("positions_sha256"), TEXT("uv_sha256"), TEXT("colors_sha256"), TEXT("packed_tangents_sha256")})
        F.Check(
            TEXT(
                "All source vertex attributes including unused vertex0 produce identical actual native readback fingerprints"),
            !After->GetStringField(Hash).IsEmpty() && After->GetStringField(Hash) == Before->GetStringField(Hash));
    F.Check(TEXT("Oriented native corner readback matches independently hashed mutated raw source indices"),
        After->GetStringField(TEXT("indices_sha256")) == IndexDigest &&
            After->GetStringField(TEXT("indices_sha256")) != Before->GetStringField(TEXT("indices_sha256")));
    for (const TCHAR* Flag : {TEXT("editable_import_qualified"), TEXT("editable_asset_qualified"),
             TEXT("original_defaults_qualified"), TEXT("dependencies_qualified"), TEXT("full_asset_qualified")})
        F.Check(TEXT("Private geometry success cannot publish or qualify a full asset"), !Result->GetBoolField(Flag));
    C.Same(F);
    F.Final();
    return F.Good && !HasAnyErrors();
}

// Append inside existing automation guard of P4OriginalMeshBoundaryCoverageAutomation.cpp.
// Contract inputs are independent copies of authentic decoded plain data.
// No source bytes, captured manifest, native descriptors or UObject are changed.

#endif

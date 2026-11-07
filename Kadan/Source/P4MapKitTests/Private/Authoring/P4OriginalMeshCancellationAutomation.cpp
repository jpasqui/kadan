// Native original mesh cancellation and output-preservation assertions.
// The caller supplies the header file of an exact original mesh.
#include "P4OriginalMeshNegativeTests.h"
#include "Misc/AutomationTest.h"
#include "Async/Async.h"

#if WITH_DEV_AUTOMATION_TESTS && P4_MAPKIT_TESTS
namespace P4OriginalMeshNegativeTests
{
FP4OriginalMeshSource OriginalMeshTestSentinel(const FP4OriginalMeshSource& Fixture)
{
    FP4OriginalMeshSource S;
    S.Digest = TEXT("sentinel-digest");
    S.Package = TEXT("sentinel-package");
    S.ExportObject = TEXT("sentinel-export");
    S.Render.Start = 17;
    S.Render.End = 19;
    S.Render.RenderDataDecoded = true;
    S.Render.LODs.resize(1);
    S.Render.LODs[0].Buffers.PositionValues.push_back({{-0.f, 2.f, 3.f}});
    FP4OriginalMeshMaterial M;
    M.Slot.NativeSlot = TEXT("sentinel-native");
    M.Slot.ImportedSlot = TEXT("sentinel-imported");
    M.Slot.ObjectIndex = -23;
    M.NativeName.SourceIndex = 29;
    M.ImportedName.SourceNumber = 31;
    M.UVDensities = {{-0.f, 5.f, 7.f, 11.f}};
    S.Materials.Add(M);
    S.OtherTaggedProperties.Add(TEXT("sentinel-field"));
    S.MaterialsValidated = true;
    S.RenderDecoded = true;
    return S;
}
bool OriginalMeshTestUnchanged(const FP4OriginalMeshSource& S, const FP4OriginalMeshSource& Before)
{
    if (S.Digest != Before.Digest || S.Package != Before.Package || S.ExportObject != Before.ExportObject ||
        S.MaterialsValidated != Before.MaterialsValidated || S.RenderDecoded != Before.RenderDecoded ||
        S.OtherTaggedProperties != Before.OtherTaggedProperties || S.Materials.Num() != 1 ||
        S.Render.LODs.size() != 1 || S.Render.Start != 17 || S.Render.End != 19 || !S.Render.RenderDataDecoded ||
        S.Render.LODs[0].Buffers.PositionValues.size() != 1)
        return false;
    const auto& M = S.Materials[0];
    const auto& B = Before.Materials[0];
    return M.Slot.NativeSlot == B.Slot.NativeSlot && M.Slot.ImportedSlot == B.Slot.ImportedSlot &&
        M.Slot.ObjectIndex == B.Slot.ObjectIndex && M.NativeName.SourceIndex == B.NativeName.SourceIndex &&
        M.ImportedName.SourceNumber == B.ImportedName.SourceNumber &&
        FMemory::Memcmp(M.UVDensities.data(), B.UVDensities.data(), 16) == 0 &&
        FMemory::Memcmp(S.Render.LODs[0].Buffers.PositionValues[0].data(),
            Before.Render.LODs[0].Buffers.PositionValues[0].data(), 12) == 0;
}
} //namespace P4OriginalMeshNegativeTests

bool P4TestOriginalMeshCancellation(FAutomationTestBase& Test, const FString& HeaderFile, const FString& Export)
{
    FString Error;
    FP4OriginalMeshSource Fixture;
    int32 ProgressCalls = 0;
    const bool Control = P4DecodeOriginalMesh(HeaderFile, FString(), Export, Fixture, Error,
        [&]()
        {
            ++ProgressCalls;
            return false;
        });
    Test.TestTrue(TEXT("Source control decodes without staging/default publication"), Control);
    if (!Control)
    {
        Test.AddError(Error);
        return false;
    }
    Test.TestTrue(
        TEXT("Bounded fixture has observable cancellation boundaries"), ProgressCalls > 0 && ProgressCalls <= 4096);
    if (ProgressCalls <= 0 || ProgressCalls > 4096)
        return false;
    const FP4OriginalMeshSource Before = P4OriginalMeshNegativeTests::OriginalMeshTestSentinel(Fixture);
    for (int32 CancelAt = 1; CancelAt <= ProgressCalls; ++CancelAt)
    {
        FP4OriginalMeshSource Out = Before;
        int32 Calls = 0;
        const bool Accepted = P4DecodeOriginalMesh(
            HeaderFile, FString(), Export, Out, Error, [&]() { return ++Calls >= CancelAt; });
        Test.TestFalse(TEXT("Every requested cancellation refuses"), Accepted);
        Test.TestTrue(TEXT("Cancellation has a real refusal diagnostic"), !Error.IsEmpty());
        Test.TestTrue(TEXT("Every cancellation leaves caller output unchanged, including signed zero"),
            P4OriginalMeshNegativeTests::OriginalMeshTestUnchanged(Out, Before));
    }
    FP4OriginalMeshSource OffThread = Before;
    FString ThreadError;
    const bool ThreadAccepted = Async(EAsyncExecution::Thread,
        [&]()
        {
            return P4DecodeOriginalMesh(HeaderFile, FString(), Export, OffThread, ThreadError);
        }).Get();
    Test.TestFalse(TEXT("Off-thread staging acquisition refuses"), ThreadAccepted);
    Test.TestEqual(TEXT("Off-thread error identifies actual contract"), ThreadError,
        FString(TEXT("original-mesh-editor-thread-required")));
    Test.TestTrue(TEXT("Off-thread refusal leaves output unchanged"),
        P4OriginalMeshNegativeTests::OriginalMeshTestUnchanged(OffThread, Before));
    return true;
}
#endif

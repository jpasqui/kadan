#include "P4MeshSourceReconstruction.h"
#if WITH_DEV_AUTOMATION_TESTS && P4_MAPKIT_TESTS
#include "P4StaticMeshBuild.h"
#include "P4StaticMeshContent.h"
#include "P4OriginalMeshImport.h"
#include "P4OriginalRecord.h"
#include "P4AuthoringRows.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "PackageTools.h"

namespace
{
FP4OriginalMeshSource Fixture(bool High, int32 ColorKind)
{
    FP4OriginalMeshSource S;
    auto& P = S.Render;
    P.RenderDataDecoded = true;
    P.Bounds = {{.5f, .5f, -0.f, .5f, .5f, -0.f, .70710677f}};
    P.ScreenSizes[0] = High ? .31415927f : -0.f;
    P4MapKit::FStaticMeshMaterial M;
    M.Slot = "Slot";
    P.Materials.push_back(M);
    FP4OriginalMeshMaterial Slot;
    Slot.Slot.NativeSlot = TEXT("Slot");
    Slot.Slot.ImportedSlot = TEXT("Slot");
    S.Materials.Add(Slot);
    P4MapKit::FStaticMeshLOD L;
    L.HasGeometry = true;
    auto& B = L.Buffers;
    B.Vertices = 3;
    B.TexCoords = 1;
    B.HighPrecisionTangents = High;
    B.FullPrecisionUV = High;
    B.Main.Wide = High;
    B.PositionValues = {{{0.f, 0.f, 0.f}}, {{1.f, 0.f, 0.f}}, {{0.f, 1.f, 0.f}}};
    const int16 Q = High ? MAX_int16 : MAX_int8;
    B.TangentValues = {{{0, 0, 0, Q, 0, 0, Q, int16(-Q)}}, {{Q, 0, 0, Q, 0, 0, 0, Q}}, {{Q, 0, 0, Q, 0, 0, Q, Q}}};
    B.UVValues = {{{{0.f, 0.f}}, {{1.f, 0.f}}, {{0.f, 1.f}}}};
    B.Main.Indices = {0, 1, 2};
    if (ColorKind)
    {
        B.ColorVertices = 3;
        B.ColorRGBA = ColorKind == 1
            ? std::vector<std::array<uint8, 4>>{{{255, 0, 0, 100}}, {{0, 255, 0, 180}}, {{0, 0, 255, 255}}}
            : std::vector<std::array<uint8, 4>>(3, {{255, 255, 255, 255}});
        if (ColorKind == 2)
            B.TangentValues.assign(3, {{Q, 0, 0, Q, 0, 0, Q, Q}});
    }
    P4MapKit::FStaticMeshSection Section;
    Section.Triangles = 1;
    Section.MaxVertex = 2;
    Section.Collision = false;
    Section.Shadow = false;
    Section.ForceOpaque = true;
    Section.RayTracing = false;
    L.Sections.push_back(Section);
    P.LODs.push_back(L);
    return S;
}
bool Exact(FAutomationTestBase& T, UStaticMesh* Mesh, const FP4OriginalMeshSource& S)
{
    if (!T.TestNotNull(TEXT("Actual built/reloaded mesh"), Mesh) || !Mesh->RenderData ||
        Mesh->RenderData->LODResources.Num() != 1)
        return false;
    const auto& B = S.Render.LODs[0].Buffers;
    auto& R = Mesh->RenderData->LODResources[0];
    auto& V = R.VertexBuffers.StaticMeshVertexBuffer;
    bool Good = V.GetUseHighPrecisionTangentBasis() == B.HighPrecisionTangents &&
        V.GetUseFullPrecisionUVs() == B.FullPrecisionUV && R.IndexBuffer.Is32Bit() == B.Main.Wide &&
        R.IndexBuffer.GetNumIndices() == 3 &&
        R.VertexBuffers.ColorVertexBuffer.GetNumVertices() == (B.ColorRGBA.empty() ? 0u : 3u);
    for (int32 I = 0; I < 3 && Good; ++I)
    {
        const uint32 At = R.IndexBuffer.GetIndex(I);
        const FVector P = R.VertexBuffers.PositionVertexBuffer.VertexPosition(At);
        Good &= FMemory::Memcmp(&P.X, B.PositionValues[I].data(), 12) == 0;
        const FVector2D UV = V.GetVertexUV(At, 0);
        Good &= FMemory::Memcmp(&UV.X, B.UVValues[0][I].data(), 8) == 0;
        for (int32 K = 0; K < 8; ++K)
        {
            const int16 Actual = B.HighPrecisionTangents ? static_cast<int16*>(V.GetTangentData())[At * 8 + K]
                                                         : static_cast<int8*>(V.GetTangentData())[At * 8 + K];
            Good &= Actual == B.TangentValues[I][K];
        }
        if (!B.ColorRGBA.empty())
        {
            const FColor C = R.VertexBuffers.ColorVertexBuffer.VertexColor(At);
            const auto& Wanted = B.ColorRGBA[I];
            Good &= C == FColor(Wanted[0], Wanted[1], Wanted[2], Wanted[3]);
        }
    }
    const auto& Bounds = Mesh->RenderData->Bounds;
    const float Values[] = {Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z, Bounds.BoxExtent.X, Bounds.BoxExtent.Y,
        Bounds.BoxExtent.Z, Bounds.SphereRadius};
    Good &= FMemory::Memcmp(Values, S.Render.Bounds.data(), sizeof Values) == 0;
    Good &= FMemory::Memcmp(&Mesh->RenderData->ScreenSize[0].Default, &S.Render.ScreenSizes[0], 4) == 0;
    Good &= R.Sections.Num() == 1 && R.Sections[0].MaterialIndex == 0 && !R.Sections[0].bEnableCollision &&
        !R.Sections[0].bCastShadow && R.Sections[0].bForceOpaque && !R.Sections[0].bVisibleInRayTracing;
    return T.TestTrue(
        TEXT("Independent exact packed frames/positions/UVs/colors/precision/index width/bounds/screens/flags"), Good);
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshSourceReconstructionTest, "P4MapKit.Quality.MeshSourceReconstruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshSourceReconstructionTest::RunTest(const FString&)
{
    for (bool High : {false, true})
        for (int32 Color : {0, 1, 2})
        {
            const auto Source = Fixture(High, Color);
            const FString Name = FString::Printf(TEXT("Precision%dColor%d"), High, Color);
            const FString PackageName = TEXT("/Game/__KadanMeshReconstructionTests/") + Name;
            UPackage* Package = CreatePackage(*PackageName);
            FP4MeshBuildReport Report;
            FString Error;
            UStaticMesh* Mesh = P4BuildOriginalMesh(
                Source, Package, FName(*Name), RF_Public | RF_Standalone, Report, Error);
            if (!TestTrue(TEXT("Zero-source build returns exact measured equality"),
                    Mesh && Report.SourceReconstructed && Report.Equal && Exact(*this, Mesh, Source)))
            {
                AddError(Error);
                return false;
            }
            // Repeated ordinary builds use the per-mesh hook; source data stays intact.
            Mesh->Build(true);
            if (!Exact(*this, Mesh, Source))
                return false;
            TestEqual(TEXT("Repeated stock builds preserve the source lightmap version"), Mesh->LightmapUVVersion, 0);
            Mesh->LightmapUVVersion = 4;
            TestFalse(TEXT("An actual lightmap-version edit is refused without a proven native build"),
                P4RestoreMeshSourceReconstruction(Mesh, Error));
            TestEqual(TEXT("Lightmap setting edit remains the caller's value"), Mesh->LightmapUVVersion, 4);
            Mesh->LightmapUVVersion = 0;
            Mesh->Build(true);
            if (!Exact(*this, Mesh, Source))
                return false;
            const FString File = FPackageName::LongPackageNameToFilename(
                PackageName, FPackageName::GetAssetPackageExtension());
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
            if (!TestTrue(TEXT("Ordinary native save persists source descriptions and tiny layout metadata"),
                    UPackage::SavePackage(
                        Package, Mesh, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError)))
                return false;
            Package->SetDirtyFlag(false);
            Mesh = nullptr;
            FText UnloadError;
            if (!TestTrue(TEXT("Actual package unloads before a new asset load"),
                    UPackageTools::UnloadPackages({Package}, UnloadError)))
                return false;
            Mesh = LoadObject<UStaticMesh>(nullptr, *(PackageName + TEXT(".") + Name));
            if (!Exact(*this, Mesh, Source))
                return false;
            Mesh->Build(true);
            if (!Exact(*this, Mesh, Source))
                return false;
            // Modified descriptions must not be silently repaired to original bounds/settings.
            FStaticMeshAttributes A(*Mesh->GetMeshDescription(0));
            A.GetVertexPositions()[FVertexID(0)].X = 9.f;
            Mesh->CommitMeshDescription(0);
            TestFalse(
                TEXT("Edited source is rejected without changing it"), P4RestoreMeshSourceReconstruction(Mesh, Error));
            TestEqual(TEXT("Edited source gets a specific refusal"), Error,
                FString(TEXT("mesh-reconstruction-source-edited")));
        }
    UPackage* ForeignPackage = CreatePackage(TEXT("/Game/__KadanMeshReconstructionTests/ForeignNoMetadata"));
    UStaticMesh* Foreign = NewObject<UStaticMesh>(ForeignPackage);
    FString Error;
    TestNull(TEXT("Fresh foreign fixture has no metadata object"),
        FindObjectFast<UMetaData>(ForeignPackage, FName(NAME_PackageMetaData)));
    TestFalse(TEXT("Creator/foreign meshes without an owned record are untouched"),
        P4RestoreMeshSourceReconstruction(Foreign, Error));
    TestFalse(TEXT("Foreign mesh acquired no reconstructed data"), Foreign->RenderData.IsValid());
    TestNull(TEXT("Refusal did not create metadata in the foreign package"),
        FindObjectFast<UMetaData>(ForeignPackage, FName(NAME_PackageMetaData)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4LargeNarrowMeshSource, "P4MapKit.Quality.LargeNarrowMeshSourceReconstruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4LargeNarrowMeshSource::RunTest(const FString&)
{
    auto Source = Fixture(false, 2);
    auto& LOD = Source.Render.LODs[0];
    LOD.Buffers.Main.Indices.clear();
    for (int32 Triangle = 0; Triangle < 30000; ++Triangle)
        LOD.Buffers.Main.Indices.insert(LOD.Buffers.Main.Indices.end(), {0, 1, 2});
    LOD.Sections[0].Triangles = 30000;
    const FString PackageName = TEXT("/Game/__KadanMeshReconstructionTests/LargeNarrow");
    UPackage* Package = CreatePackage(*PackageName);
    FP4MeshBuildReport Report;
    FString Error;
    UStaticMesh* Mesh = P4BuildOriginalMesh(
        Source, Package, TEXT("LargeNarrow"), RF_Public | RF_Standalone, Report, Error);
    if (!TestNotNull(TEXT("90K triangle corners fit the source's three narrow-indexed vertices"), Mesh))
    {
        AddError(Error);
        return false;
    }
    const auto Check = [&]()
    {
        FP4MeshBuildReport Readback;
        return TestTrue(TEXT("All source corners, precision, sections, bounds and screens compare exactly"),
                   P4CompareOriginalMeshBuild(Source, Mesh, Readback, Error) && Readback.Equal) &&
            TestFalse(TEXT("Source keeps its 16-bit main index format"),
                Mesh->RenderData->LODResources[0].IndexBuffer.Is32Bit()) &&
            TestEqual(TEXT("Reconstruction shares the original vertices"),
                Mesh->RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer.GetNumVertices(), uint32(3));
    };
    if (!Check())
        return false;
    Mesh->Build(true);
    if (!Check())
        return false;
    const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    if (!TestTrue(TEXT("Narrow source reconstruction saves"),
            UPackage::SavePackage(
                Package, Mesh, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError)))
        return false;
    Package->SetDirtyFlag(false);
    Mesh = nullptr;
    FText Why;
    if (!TestTrue(TEXT("Unload before cold reload"), UPackageTools::UnloadPackages({Package}, Why)))
        return false;
    Mesh = LoadObject<UStaticMesh>(nullptr, *(PackageName + TEXT(".LargeNarrow")));
    if (!TestNotNull(TEXT("Cold reload reconstructs the narrow source"), Mesh) || !Check())
        return false;
    Mesh->Build(true);
    return Check();
}

namespace
{
bool RealSource(FAutomationTestBase& Test, FP4OriginalMeshSource& Out, FString& Header)
{
    Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MESH_GAME"));
    if (Header.IsEmpty())
    {
        Test.AddError(TEXT(
            "Real mesh acceptance requires P4_NATIVE_MESH_GAME pointing to an existing converted WoodFenceM04 package or conversion-cache root."));
        return false;
    }
    if (!Header.EndsWith(TEXT(".uasset"), ESearchCase::IgnoreCase))
        Header /= TEXT(
            "Carrot4/Content/Carrot4/Terrain/Area/Area500/Objects/stg_Area500_WoodFenceM04/SM_stg_Area500_WoodFenceM04.uasset");
    FString Error;
    if (!Test.TestTrue(TEXT("Retained actual original package decodes without conversion"),
            P4DecodeOriginalMesh(Header, FString(), FString(), Out, Error)))
    {
        Test.AddError(Error);
        return false;
    }
    int32 Zeros = 0;
    for (const auto& L : Out.Render.LODs)
        for (const auto& T : L.Buffers.TangentValues)
            Zeros += (T[0] == 0 && T[1] == 0 && T[2] == 0) || (T[4] == 0 && T[5] == 0 && T[6] == 0);
    return Test.TestTrue(TEXT("Real game package contains the original zero-frame failure"), Zeros > 0);
}
bool RealExact(FAutomationTestBase& Test, UStaticMesh* Mesh, const FP4OriginalMeshSource& Source)
{
    FP4MeshBuildReport Compared;
    FString Error;
    bool Good = Test.TestTrue(TEXT("Every actual source LOD has complete render readback"),
        P4CompareOriginalMeshBuild(Source, Mesh, Compared, Error));
    if (!Good)
    {
        Test.AddError(Error);
        return false;
    }
    Good &= Test.TestTrue(
        TEXT("Actual packed corners/layout/sections/bounds/screens are exact without weld/tangent allowances"),
        Compared.Equal);
    Good &= Test.TestTrue(
        TEXT("Readback covers all actual LODs"), Compared.LODs.Num() == int32(Source.Render.LODs.size()));
    Good &= Test.TestTrue(TEXT("Actual source baseline remains unchanged"), P4OriginalMeshBaselineUnchanged(Mesh));
    // QueryOriginalAsset reports provenance (Recorded/Dirty); it deliberately
    // does not infer equality from saved metadata. Use the explicit comparison.
    const FP4OriginalDiagnostic Diagnostic = P4DiagnoseOriginalAsset(Mesh);
    const bool RecordEqual = Diagnostic.Error.IsEmpty() && Diagnostic.Provenance.IsOriginal() &&
        Diagnostic.Provenance.Record.SourceDigest == Source.Digest &&
        Diagnostic.Provenance.Record.ComparisonSchema == TEXT("mesh-state-v1") &&
        Diagnostic.Comparison == EP4OriginalComparison::Equal;
    if (!RecordEqual)
        Test.AddInfo(FString::Printf(
            TEXT(
                "Mesh record comparison: error=%s schema=%s state=%d comparison=%d source_expected=%s source_recorded=%s digest_expected=%s digest_current=%s"),
            *Diagnostic.Error, *Diagnostic.Provenance.Record.ComparisonSchema, int32(Diagnostic.Provenance.State),
            int32(Diagnostic.Comparison), *Source.Digest, *Diagnostic.Provenance.Record.SourceDigest,
            *Diagnostic.Provenance.Record.ComparisonDigest, *Diagnostic.CurrentDigest));
    Good &= Test.TestTrue(
        TEXT("Explicit native original comparison independently verifies unchanged asset"), RecordEqual);
    return Good;
}
bool BuildRealExact(FAutomationTestBase& Test, UStaticMesh* Mesh, const FP4OriginalMeshSource& Source)
{
    const auto Before = P4StaticMeshStateParts(Mesh);
    Mesh->Build(true);
    const auto After = P4StaticMeshStateParts(Mesh);
    int32 Changed = 0;
    for (const auto& Part : Before)
    {
        const FString* Current = After.Find(Part.Key);
        if (!Current || *Current != Part.Value)
        {
            ++Changed;
            Test.AddInfo(FString::Printf(TEXT("Mesh state field changed %s: BEFORE %s AFTER %s"), *Part.Key,
                *Part.Value, Current ? **Current : TEXT("absent")));
        }
    }
    for (const auto& Part : After)
        if (!Before.Contains(Part.Key))
        {
            ++Changed;
            Test.AddInfo(TEXT("Mesh state field added ") + Part.Key + TEXT(": ") + Part.Value);
        }
    Test.AddInfo(FString::Printf(TEXT("Mesh state fields changed by ordinary Build: %d"), Changed));
    return RealExact(Test, Mesh, Source);
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RealMeshReconstructionImportTest,
    "P4MapKit.Acceptance.RealMeshReconstructionImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RealMeshReconstructionImportTest::RunTest(const FString&)
{
    FP4OriginalMeshSource Source;
    FString Header;
    if (!RealSource(*this, Source, Header))
        return false;
    const auto Imported = P4ValidateReconstructedMeshImport(Header, FString(), true);
    if (!TestTrue(TEXT("Normal native publication/fill pipeline saves the actual reconstructed mesh"),
            Imported.IsValid() && Imported.Asset && Imported.BaselineRead && Imported.Equality == TEXT("exact")))
    {
        AddError(Imported.ErrorDetail);
        return false;
    }
    if (!RealExact(*this, Imported.Asset, Source))
        return false;
    if (!TestTrue(
            TEXT("Actual game asset is saved at its original path"), FPackageName::DoesPackageExist(Source.Package)))
        return false;
    return BuildRealExact(*this, Imported.Asset, Source);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RealMeshReconstructionReloadTest,
    "P4MapKit.Acceptance.RealMeshReconstructionReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RealMeshReconstructionReloadTest::RunTest(const FString&)
{
    FP4OriginalMeshSource Source;
    FString Header;
    if (!RealSource(*this, Source, Header))
        return false;
    // This phase must run in a NEW editor. It neither imports nor rebuilds before
    // asserting exact source readback and persistent original baseline.
    if (!TestNull(TEXT("Cold gate starts with actual asset not already loaded"),
            FindObject<UStaticMesh>(nullptr, *Source.ExportObject)))
        return false;
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Source.ExportObject);
    if (!TestNotNull(TEXT("Actual saved package loads in the new editor"), Mesh) || !RealExact(*this, Mesh, Source))
        return false;
    if (!BuildRealExact(*this, Mesh, Source))
        return false;
    return BuildRealExact(*this, Mesh, Source);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RealMeshReconstructionProductionTest,
    "P4MapKit.Acceptance.RealMeshReconstructionProductionImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RealMeshReconstructionProductionTest::RunTest(const FString&)
{
    FP4OriginalMeshSource Source;
    FString Header;
    if (!RealSource(*this, Source, Header))
        return false;
    // Use a different retained zero-frame package which has not yet imported.
    // Reusing an existing original would skip the admission branch under test.
    if (!TestFalse(TEXT("Production admission regression uses an unimported actual package"),
            FPackageName::DoesPackageExist(Source.Package)))
        return false;
    const FString Reply = UP4AuthoringTools::NativeImportOriginalMesh(Header, FString(), true);
    TSharedPtr<FJsonObject> J;
    if (!TestTrue(TEXT("Ordinary reflected import endpoint admits and saves the qualified source"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Reply), J) && J.IsValid() &&
                J->GetBoolField(TEXT("valid"))))
    {
        AddError(Reply.Left(2048));
        return false;
    }
    UStaticMesh* Mesh = FindObject<UStaticMesh>(nullptr, *Source.ExportObject);
    if (!TestNotNull(TEXT("Production endpoint publishes the actual original mesh"), Mesh) ||
        !RealExact(*this, Mesh, Source))
        return false;
    if (!TestTrue(
            TEXT("Production import saved at the original game path"), FPackageName::DoesPackageExist(Source.Package)))
        return false;
    return BuildRealExact(*this, Mesh, Source);
}
#endif

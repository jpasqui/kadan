#include "P4StaticMeshStaging.h"
#if WITH_DEV_AUTOMATION_TESTS && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "StaticMeshAttributes.h"

namespace
{
P4MapKit::FStaticMeshRenderPayload Fixture(bool High)
{
    using namespace P4MapKit;
    FStaticMeshRenderPayload P;
    P.RenderDataDecoded = true;
    FStaticMeshMaterial M;
    M.Slot = "NativeSlot";
    M.ObjectIndex = -3;
    P.Materials.push_back(M);
    P.Materials.push_back(M);
    FStaticMeshLOD L;
    L.HasGeometry = true;
    auto& B = L.Buffers;
    B.Vertices = 7;
    B.TexCoords = 2;
    B.ColorVertices = 7;
    B.HighPrecisionTangents = High;
    B.PositionValues = {
        {{0, 0, -0.f}}, {{1, 0, 0}}, {{0, 1, 0}}, {{0, 0, -0.f}}, {{1, 0, 0}}, {{0, 1, 0}}, {{2, 2, 2}}};
    const int16 Scale = High ? MAX_int16 : MAX_int8;
    B.TangentValues.resize(7, {{Scale, 0, 0, Scale, 0, 0, Scale, Scale}});
    B.TangentValues[3] = {{0, Scale, 0, Scale, 0, 0, Scale, int16(-Scale)}};
    B.UVValues.resize(2);
    for (int32 V = 0; V < 7; ++V)
    {
        B.UVValues[0].push_back({{V == 0 ? -0.f : float(V) / 8.f, .5f}});
        B.UVValues[1].push_back({{float(V), float(V + 1)}});
        B.ColorRGBA.push_back({{uint8(V * 30), uint8(255 - V * 20), uint8(V + 1), uint8(V * 40)}});
    }
    B.Main.Indices = {0, 1, 2, 3, 4, 5, 0, 1, 2};
    P4MapKit::FStaticMeshSection S;
    S.Triangles = 1;
    S.MaxVertex = 2;
    S.Collision = true;
    L.Sections.push_back(S);
    S.MaterialIndex = 1;
    S.FirstIndex = 3;
    S.Triangles = 2;
    S.MaxVertex = 5;
    L.Sections.push_back(S);
    P.LODs.push_back(L);
    P.LODs.push_back(L);
    return P;
}
TArray<FP4ExactMeshMaterialSlot> Slots()
{
    FP4ExactMeshMaterialSlot S;
    S.NativeSlot = TEXT("NativeSlot");
    S.ImportedSlot = TEXT("SharedImportedSlot");
    S.ObjectIndex = -3;
    return {S, S};
}
} //namespace

bool P4TestStaticMeshStaging(FAutomationTestBase& Test)
{
    bool Good = true;
    const auto Check = [&](const TCHAR* Message, bool Value)
    {
        Good = Test.TestTrue(Message, Value) && Good;
        return Value;
    };
    const auto MaterialSlots = Slots();
    for (bool High : {false, true})
    {
        auto Source = Fixture(High);
        FP4PrivateMeshStage Stage;
        FP4MeshStageReport Report;
        FString Error;
        if (Check(TEXT("Typed source stages all LODs/seams/duplicate triangles/unused vertices"),
                P4StagePrivateMeshDescriptions(Source, MaterialSlots, Stage, Report, Error)))
        {
            Check(TEXT("Geometry readback verified while asset/default/dependency admission stays false"),
                Report.GeometryAttributesVerified && !Report.EditableAssetQualified &&
                    !Report.OriginalDefaultsQualified && !Report.DependenciesQualified &&
                    Report.UnrepresentedFields.Num() > 0);
            Check(TEXT("Source LOD and corner denominator includes duplicate triangles and unused source values"),
                Stage.LODs.Num() == 2 && Report.Vertices == 14 && Report.Corners == 18 && Report.UnusedVertices == 2 &&
                    Report.Sections == 4);
            Check(TEXT("Duplicate imported slot labels preserve separate section identities"),
                Stage.LODs[0].Sections.Num() == 2 && Stage.LODs[0].Sections[0] != Stage.LODs[0].Sections[1]);
            Check(TEXT("Independent engine attribute readback passes unchanged stage"),
                P4VerifyPrivateMeshDescriptions(Source, MaterialSlots, Stage, Report, Error));
        }
        const auto Refuse = [&](const P4MapKit::FStaticMeshRenderPayload& Corrupt, const TCHAR* Expected,
                                const FP4MeshStageLimits& Limits = FP4MeshStageLimits{},
                                const TFunction<bool()>& Cancel = TFunction<bool()>{})
        {
            FP4PrivateMeshStage Existing;
            Existing.LODs.AddDefaulted();
            FP4MeshStageReport ExistingReport;
            ExistingReport.Corners = 999;
            Check(TEXT("Malformed source refuses private stage"),
                !P4StagePrivateMeshDescriptions(
                    Corrupt, MaterialSlots, Existing, ExistingReport, Error, Limits, Cancel));
            Check(TEXT("Source refusal is specific"), Error == Expected);
            Check(TEXT("Refusal leaves previous stage/report untouched"),
                Existing.LODs.Num() == 1 && !Existing.LODs[0].Description && ExistingReport.Corners == 999);
        };
        auto Corrupt = Source;
        Corrupt.LODs[1].Buffers.TangentValues[0][3] = 0;
        Refuse(Corrupt, TEXT("mesh-stage-unrepresentable-packed-tangent-w"));
        Corrupt = Source;
        Corrupt.LODs[1].Buffers.TangentValues[0][7] = 1;
        Refuse(Corrupt, TEXT("mesh-stage-unrepresentable-packed-tangent-w"));
        Corrupt = Source;
        for (int32 K = 0; K < 3; ++K)
            Corrupt.LODs[0].Buffers.TangentValues[0][K] = 0;
        {
            FP4PrivateMeshStage ZeroStage;
            FP4MeshStageReport ZeroReport;
            Check(TEXT("A real zero tangent stages without changing source packed attributes"),
                P4StagePrivateMeshDescriptions(Corrupt, MaterialSlots, ZeroStage, ZeroReport, Error) &&
                    P4VerifyPrivateMeshDescriptions(Corrupt, MaterialSlots, ZeroStage, ZeroReport, Error));
        }
        Corrupt = Source;
        for (int32 K = 0; K < 3; ++K)
            Corrupt.LODs[0].Buffers.TangentValues[0][4 + K] = Corrupt.LODs[0].Buffers.TangentValues[0][K];
        Corrupt.LODs[0].Buffers.TangentValues[0][7] = -Corrupt.LODs[0].Buffers.TangentValues[0][3];
        Refuse(Corrupt, TEXT("mesh-stage-parallel-tangent-frame-with-negative-sign"));
        {
            // No color buffer means every color is opaque white: the engine drops an all-white buffer.
            auto Plain = Source;
            Plain.LODs[0].Buffers.ColorRGBA.clear();
            Plain.LODs[0].Buffers.ColorVertices = 0;
            FP4PrivateMeshStage PlainStage;
            FP4MeshStageReport PlainReport;
            if (Check(TEXT("A LOD without a color buffer stages"),
                    P4StagePrivateMeshDescriptions(Plain, MaterialSlots, PlainStage, PlainReport, Error) &&
                        PlainStage.LODs.Num() == 2))
            {
                const FMeshDescription& Description = *PlainStage.LODs[0].Description;
                FStaticMeshConstAttributes Attributes(Description);
                bool White = true;
                int32 Seen = 0;
                for (const FVertexInstanceID Instance : Description.VertexInstances().GetElementIDs())
                {
                    White = White && Attributes.GetVertexInstanceColors()[Instance] == FVector4(1.f, 1.f, 1.f, 1.f);
                    ++Seen;
                }
                Check(TEXT("Every corner of a LOD without a color buffer is opaque white"),
                    White && Seen == Description.VertexInstances().Num() && Seen > 0);
                const FMeshDescription& Colored = *PlainStage.LODs[1].Description;
                FStaticMeshConstAttributes Kept(Colored);
                bool AnyColor = false;
                for (const FVertexInstanceID Instance : Colored.VertexInstances().GetElementIDs())
                    AnyColor = AnyColor || !(Kept.GetVertexInstanceColors()[Instance] == FVector4(1.f, 1.f, 1.f, 1.f));
                Check(TEXT("The LOD that has a color buffer keeps its colors"), AnyColor);
            }
            Corrupt = Source;
            for (auto& Color : Corrupt.LODs[0].Buffers.ColorRGBA)
                Color = {{255, 255, 255, 255}};
            {
                FP4PrivateMeshStage WhiteStage;
                FP4MeshStageReport WhiteReport;
                Check(TEXT("A present white buffer stays distinct from absent source colors"),
                    P4StagePrivateMeshDescriptions(Corrupt, MaterialSlots, WhiteStage, WhiteReport, Error) &&
                        P4VerifyPrivateMeshDescriptions(Corrupt, MaterialSlots, WhiteStage, WhiteReport, Error));
            }
            Corrupt = Source;
            Corrupt.LODs[0].Buffers.ColorRGBA.pop_back();
            Refuse(Corrupt, TEXT("mesh-stage-color-count-invalid"));
        }
        Corrupt = Source;
        Corrupt.LODs[0].Buffers.UVValues[1].pop_back();
        Refuse(Corrupt, TEXT("mesh-stage-uv-count-invalid"));
        Corrupt = Source;
        Corrupt.LODs[0].Buffers.Main.Indices[1] = 0;
        Refuse(Corrupt, TEXT("mesh-stage-index-or-repeated-corner-invalid"));
        Corrupt = Source;
        Corrupt.LODs[0].Buffers.Main.Indices[0] = 7;
        Refuse(Corrupt, TEXT("mesh-stage-index-or-repeated-corner-invalid"));
        Corrupt = Source;
        Corrupt.LODs[1].Sections[1].FirstIndex = 0;
        Refuse(Corrupt, TEXT("mesh-stage-section-invalid"));
        FP4MeshStageLimits Budget;
        Budget.EstimatedBytes = 1;
        Refuse(Source, TEXT("mesh-stage-total-budget-exceeded"), Budget);
        Refuse(Source, TEXT("mesh-stage-cancelled"), {}, []() { return true; });
        unsigned Calls = 0;
        FP4PrivateMeshStage CountStage;
        Check(TEXT("Source staging can expose bounded cancellation boundaries"),
            P4StagePrivateMeshDescriptions(Source, MaterialSlots, CountStage, Report, Error, {},
                [&]()
                {
                    ++Calls;
                    return false;
                }));
        for (unsigned Stop = 1; Stop <= Calls; ++Stop)
        {
            unsigned At = 0;
            Refuse(Source, TEXT("mesh-stage-cancelled"), {}, [&]() { return ++At >= Stop; });
        }
        const auto Drift = [&](const TCHAR* Message, const TFunction<void(FP4PrivateMeshStage&)>& Change)
        {
            FP4PrivateMeshStage Scratch;
            FP4MeshStageReport Before;
            if (!Check(TEXT("Prepare independent drift fixture"),
                    P4StagePrivateMeshDescriptions(Source, MaterialSlots, Scratch, Before, Error)))
                return;
            Change(Scratch);
            FP4MeshStageReport Readback;
            Readback.Corners = 999;
            Check(Message, !P4VerifyPrivateMeshDescriptions(Source, MaterialSlots, Scratch, Readback, Error));
            Check(TEXT("Failed independent readback does not publish success/report"),
                !Readback.GeometryAttributesVerified && Readback.Corners == 999);
        };
        Drift(TEXT("Dropped lower LOD detected"), [](FP4PrivateMeshStage& S) { S.LODs.RemoveAt(1); });
        Drift(TEXT("Signed-zero source position drift detected"),
            [](FP4PrivateMeshStage& S)
            {
                FStaticMeshAttributes A(*S.LODs[0].Description);
                A.GetVertexPositions()[S.LODs[0].Vertices[0]].Z = 0.f;
            });
        Drift(TEXT("UV seam and signed-zero drift detected"),
            [](FP4PrivateMeshStage& S)
            {
                FStaticMeshAttributes A(*S.LODs[0].Description);
                A.GetVertexInstanceUVs().Set(S.LODs[0].Corners[0], 0, FVector2D(0.f, .5f));
            });
        Drift(TEXT("Dropped UV channel detected"),
            [](FP4PrivateMeshStage& S)
            {
                FStaticMeshAttributes A(*S.LODs[0].Description);
                A.GetVertexInstanceUVs().SetNumIndices(1);
            });
        Drift(TEXT("Dropped alpha/color detected"),
            [](FP4PrivateMeshStage& S)
            {
                FStaticMeshAttributes A(*S.LODs[0].Description);
                A.GetVertexInstanceColors()[S.LODs[0].Corners[0]] = FVector4(0, 0, 0, 1);
            });
        Drift(TEXT("Changed packed tangent detected without tolerance"),
            [](FP4PrivateMeshStage& S)
            {
                FStaticMeshAttributes A(*S.LODs[0].Description);
                A.GetVertexInstanceTangents()[S.LODs[0].Corners[0]].X = .5f;
            });
        Drift(TEXT("Nonfinite edited tangent refuses before native repacking"),
            [](FP4PrivateMeshStage& S)
            {
                FStaticMeshAttributes A(*S.LODs[0].Description);
                A.GetVertexInstanceTangents()[S.LODs[0].Corners[0]].X = std::numeric_limits<float>::quiet_NaN();
            });
        Drift(TEXT("Changed handedness detected"),
            [](FP4PrivateMeshStage& S)
            {
                FStaticMeshAttributes A(*S.LODs[0].Description);
                A.GetVertexInstanceBinormalSigns()[S.LODs[0].Corners[0]] = -1.f;
            });
        Drift(TEXT("Swapped oriented corner mapping detected"),
            [](FP4PrivateMeshStage& S) { Swap(S.LODs[0].Corners[0], S.LODs[0].Corners[1]); });
        Drift(TEXT("Same-position source vertex identity drift detected"),
            [](FP4PrivateMeshStage& S) { Swap(S.LODs[0].Vertices[0], S.LODs[0].Vertices[3]); });
        Drift(TEXT("Section order drift detected despite duplicate labels"),
            [](FP4PrivateMeshStage& S) { Swap(S.LODs[0].Sections[0], S.LODs[0].Sections[1]); });
        Drift(TEXT("Material slot label drift detected"),
            [](FP4PrivateMeshStage& S)
            {
                FStaticMeshAttributes A(*S.LODs[0].Description);
                A.GetPolygonGroupMaterialSlotNames()[S.LODs[0].Sections[0]] = TEXT("WrongImportedSlot");
            });
        Drift(TEXT("Unused source attributes remain verified"),
            [](FP4PrivateMeshStage& S)
            {
                FStaticMeshAttributes A(*S.LODs[0].Description);
                A.GetVertexInstanceUVs().Set(S.LODs[0].UnusedInstances[6], 1, FVector2D(0, 0));
            });
        Drift(TEXT("Absent private description does not become an empty successful LOD"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].Description.Reset(); });
        Drift(TEXT("Missing native UV attribute refuses instead of supplying default coordinates"),
            [](FP4PrivateMeshStage& S)
            {
                S.LODs[1].Description->VertexInstanceAttributes().UnregisterAttribute(
                    MeshAttribute::VertexInstance::TextureCoordinate);
            });
        Drift(TEXT("Dropped source vertex identity mapping detected"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].Vertices.Pop(); });
        Drift(TEXT("Dropped oriented source corner mapping detected"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].Corners.Pop(); });
        Drift(TEXT("Dropped unused attribute identity mapping detected"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].UnusedInstances.Pop(); });
        Drift(TEXT("Dropped section identity mapping detected"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].Sections.Pop(); });
        Drift(
            TEXT("Dropped source polygon mapping detected"), [](FP4PrivateMeshStage& S) { S.LODs[1].Polygons.Pop(); });
        Drift(TEXT("Extra native vertex cannot silently expand the source topology"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].Description->CreateVertex(); });
        Drift(TEXT("Extra unconnected native instance cannot hide outside source mappings"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].Description->CreateVertexInstance(S.LODs[1].Vertices[0]); });
        Drift(TEXT("Invalid source vertex identity refuses before attribute indexing"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].Vertices[0] = FVertexID(MAX_int32); });
        Drift(TEXT("Duplicate source vertex identity cannot weld separate source vertices"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].Vertices[3] = S.LODs[1].Vertices[0]; });
        Drift(TEXT("Missing unconnected source attribute instance detected"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].UnusedInstances[6] = FVertexInstanceID(INDEX_NONE); });
        Drift(TEXT("Connected corner cannot impersonate the unused source vertex"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].UnusedInstances[6] = S.LODs[1].Corners[0]; });
        Drift(TEXT("Used source vertex cannot acquire an extra unused mapping"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].UnusedInstances[0] = S.LODs[1].Corners[0]; });
        Drift(TEXT("Duplicate group mapping loses separate section identity"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].Sections[1] = S.LODs[1].Sections[0]; });
        Drift(TEXT("Duplicate polygon mapping cannot replace a source triangle"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].Polygons[2] = S.LODs[1].Polygons[1]; });
        Drift(TEXT("Invalid polygon identity refuses before native topology access"),
            [](FP4PrivateMeshStage& S) { S.LODs[1].Polygons[0] = FPolygonID(MAX_int32); });
        Drift(TEXT("Moving a native polygon between sections is detected despite duplicate material labels"),
            [](FP4PrivateMeshStage& S)
            { S.LODs[1].Description->SetPolygonPolygonGroup(S.LODs[1].Polygons[0], S.LODs[1].Sections[1]); });
        Drift(TEXT("Nonfinite edited normal refuses native repacking"),
            [](FP4PrivateMeshStage& S)
            {
                FStaticMeshAttributes A(*S.LODs[1].Description);
                A.GetVertexInstanceNormals()[S.LODs[1].Corners[0]].Z = std::numeric_limits<float>::infinity();
            });
        Drift(TEXT("Fractional handedness cannot become an implicit sign"),
            [](FP4PrivateMeshStage& S)
            {
                FStaticMeshAttributes A(*S.LODs[1].Description);
                A.GetVertexInstanceBinormalSigns()[S.LODs[1].Corners[0]] = .5f;
            });
        unsigned ReadbackCalls = 0;
        Check(TEXT("Independent native readback exposes bounded cancellation boundaries"),
            P4VerifyPrivateMeshDescriptions(Source, MaterialSlots, Stage, Report, Error, {},
                [&]()
                {
                    ++ReadbackCalls;
                    return false;
                }));
        for (unsigned Stop = 1; Stop <= ReadbackCalls; ++Stop)
        {
            unsigned At = 0;
            FP4MeshStageReport Sentinel;
            Sentinel.Corners = 999;
            Check(TEXT("Cancellation during independent readback refuses success"),
                !P4VerifyPrivateMeshDescriptions(
                    Source, MaterialSlots, Stage, Sentinel, Error, {}, [&]() { return ++At >= Stop; }));
            Check(TEXT("Cancelled readback retains previous report"),
                Error == TEXT("mesh-stage-cancelled") && Sentinel.Corners == 999 &&
                    !Sentinel.GeometryAttributesVerified);
        }
    }
    // Keep an actual successful native description alive across refusals. Checking
    // only an empty sentinel would miss deletion or mutation of an earlier import.
    auto StableSource = Fixture(false);
    FP4PrivateMeshStage StableStage;
    FP4MeshStageReport StableReport;
    FString StableError;
    if (Check(TEXT("Prepare prior successful stage for transactional refusal checks"),
            P4StagePrivateMeshDescriptions(StableSource, MaterialSlots, StableStage, StableReport, StableError)))
    {
        const FMeshDescription* const PriorDescription = StableStage.LODs[0].Description.Get();
        const auto Preserve = [&](const P4MapKit::FStaticMeshRenderPayload& Input,
                                  const TArray<FP4ExactMeshMaterialSlot>& InputSlots, const TCHAR* Expected,
                                  const FP4MeshStageLimits& Limits = FP4MeshStageLimits{})
        {
            Check(TEXT("Unqualified source refuses replacement of a valid private stage"),
                !P4StagePrivateMeshDescriptions(Input, InputSlots, StableStage, StableReport, StableError, Limits));
            Check(TEXT("Transactional source refusal explains the lost representation"), StableError == Expected);
            Check(TEXT("Source refusal retains actual prior description and verified report"),
                StableStage.LODs.Num() == 2 && StableStage.LODs[0].Description.Get() == PriorDescription &&
                    StableReport.GeometryAttributesVerified && StableReport.Vertices == 14 &&
                    StableReport.Corners == 18);
            FP4MeshStageReport Independent;
            Check(TEXT("Prior native description still independently matches every original attribute after refusal"),
                P4VerifyPrivateMeshDescriptions(StableSource, MaterialSlots, StableStage, Independent, StableError));
        };
        const auto SourceRefusal =
            [&](const TFunction<void(P4MapKit::FStaticMeshRenderPayload&)>& Change, const TCHAR* Expected)
        {
            auto Invalid = StableSource;
            Change(Invalid);
            Preserve(Invalid, MaterialSlots, Expected);
        };
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.RenderDataDecoded = false; },
            TEXT("mesh-stage-source-or-budget-unqualified"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P)
            { P.Contract = static_cast<P4MapKit::EStaticMeshRenderContract>(999); },
            TEXT("mesh-stage-source-or-budget-unqualified"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs.clear(); },
            TEXT("mesh-stage-source-or-budget-unqualified"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs.resize(9, P.LODs[0]); },
            TEXT("mesh-stage-source-or-budget-unqualified"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.Materials.clear(); },
            TEXT("mesh-stage-source-or-budget-unqualified"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.Materials.resize(4097); },
            TEXT("mesh-stage-source-or-budget-unqualified"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.Materials[0].Slot = std::string("A\0B", 3); },
            TEXT("mesh-stage-material-label-not-exactly-representable"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].HasGeometry = false; },
            TEXT("mesh-stage-lod-counts-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].CookedOut = true; },
            TEXT("mesh-stage-lod-counts-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.Vertices = MAX_uint32; },
            TEXT("mesh-stage-lod-counts-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.PositionValues.pop_back(); },
            TEXT("mesh-stage-lod-counts-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.TangentValues.pop_back(); },
            TEXT("mesh-stage-lod-counts-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.TexCoords = 0; },
            TEXT("mesh-stage-lod-counts-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.TexCoords = 9; },
            TEXT("mesh-stage-lod-counts-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.UVValues.pop_back(); },
            TEXT("mesh-stage-lod-counts-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.Main.Indices.clear(); },
            TEXT("mesh-stage-lod-counts-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.Main.Indices.pop_back(); },
            TEXT("mesh-stage-lod-counts-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Sections.clear(); },
            TEXT("mesh-stage-lod-counts-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Sections.resize(1025); },
            TEXT("mesh-stage-lod-counts-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.ColorRGBA.pop_back(); },
            TEXT("mesh-stage-color-count-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P)
            { P.LODs[1].Buffers.PositionValues[6][0] = std::numeric_limits<float>::quiet_NaN(); },
            TEXT("mesh-stage-nonfinite-position"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P)
            { P.LODs[1].Buffers.UVValues[1][6][1] = std::numeric_limits<float>::infinity(); },
            TEXT("mesh-stage-nonfinite-uv"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.TangentValues[6][0] = 128; },
            TEXT("mesh-stage-packed-tangent-range"));
        {
            auto Zero = StableSource;
            for (int32 K = 4; K < 7; ++K)
                Zero.LODs[1].Buffers.TangentValues[6][K] = 0;
            FP4PrivateMeshStage ZeroStage;
            FP4MeshStageReport ZeroReport;
            Check(TEXT("An unused vertex's zero normal stays in the verified source description"),
                P4StagePrivateMeshDescriptions(Zero, MaterialSlots, ZeroStage, ZeroReport, StableError) &&
                    P4VerifyPrivateMeshDescriptions(Zero, MaterialSlots, ZeroStage, ZeroReport, StableError));
        }
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Sections[0].MaterialIndex = -1; },
            TEXT("mesh-stage-section-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Sections[0].MaterialIndex = 2; },
            TEXT("mesh-stage-section-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Sections[0].Triangles = 0; },
            TEXT("mesh-stage-section-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Sections[0].Triangles = MAX_uint32; },
            TEXT("mesh-stage-section-extent-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Sections[1].Triangles = 1; },
            TEXT("mesh-stage-section-index-gap"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.Main.Indices[2] = 1; },
            TEXT("mesh-stage-index-or-repeated-corner-invalid"));
        SourceRefusal([](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.Main.Indices[2] = 0; },
            TEXT("mesh-stage-index-or-repeated-corner-invalid"));
        auto InvalidSlots = MaterialSlots;
        InvalidSlots.Pop();
        Preserve(StableSource, InvalidSlots, TEXT("mesh-stage-source-or-budget-unqualified"));
        InvalidSlots = MaterialSlots;
        InvalidSlots[1].NativeSlot = TEXT("WrongOriginalSlot");
        Preserve(StableSource, InvalidSlots, TEXT("mesh-stage-material-label-not-exactly-representable"));
        InvalidSlots = MaterialSlots;
        InvalidSlots[1].ImportedSlot.Reset();
        Preserve(StableSource, InvalidSlots, TEXT("mesh-stage-material-label-not-exactly-representable"));
        FP4MeshStageLimits Budget;
        Budget.Vertices = 0;
        Preserve(StableSource, MaterialSlots, TEXT("mesh-stage-source-or-budget-unqualified"), Budget);
        Budget = {};
        Budget.Vertices = 1000001;
        Preserve(StableSource, MaterialSlots, TEXT("mesh-stage-source-or-budget-unqualified"), Budget);
        Budget = {};
        Budget.Corners = 0;
        Preserve(StableSource, MaterialSlots, TEXT("mesh-stage-source-or-budget-unqualified"), Budget);
        Budget = {};
        Budget.Corners = 6000001;
        Preserve(StableSource, MaterialSlots, TEXT("mesh-stage-source-or-budget-unqualified"), Budget);
        Budget = {};
        Budget.Vertices = 13;
        Preserve(StableSource, MaterialSlots, TEXT("mesh-stage-total-budget-exceeded"), Budget);
        Budget = {};
        Budget.Corners = 17;
        Preserve(StableSource, MaterialSlots, TEXT("mesh-stage-total-budget-exceeded"), Budget);
        Budget = {};
        Budget.EstimatedBytes = 23615;
        Preserve(StableSource, MaterialSlots, TEXT("mesh-stage-total-budget-exceeded"), Budget);
        Budget.EstimatedBytes = 23616;
        Check(TEXT("Exact cumulative allocation budget admits unchanged topology without an off-by-one refusal"),
            P4VerifyPrivateMeshDescriptions(
                StableSource, MaterialSlots, StableStage, StableReport, StableError, Budget));
    }
    // Real LODs do not necessarily share UV counts or triangle counts. Retain
    // every source vertex even when the lower LOD uses only a subset of them.
    auto DistinctSource = Fixture(false);
    auto DistinctSlots = MaterialSlots;
    DistinctSlots[1].ImportedSlot = TEXT("NumberedImportedSlot_42");
    auto& Lower = DistinctSource.LODs[1];
    Lower.Buffers.Main.Indices = {3, 4, 5};
    Lower.Sections.resize(1);
    Lower.Sections[0].MaterialIndex = 1;
    Lower.Sections[0].FirstIndex = 0;
    Lower.Sections[0].MinVertex = 3;
    Lower.Sections[0].MaxVertex = 5;
    Lower.Buffers.TexCoords = 8;
    Lower.Buffers.FullPrecisionUV = true;
    for (int32 U = 2; U < 8; ++U)
        Lower.Buffers.UVValues.push_back(Lower.Buffers.UVValues[1]);
    for (int32 U = 2; U < 8; ++U)
        for (int32 V = 0; V < 7; ++V)
            Lower.Buffers.UVValues[U][V] = {{float(U * 10 + V), V == 6 ? -0.f : float(V)}};
    Lower.Buffers.UVValues[7][6][0] = std::numeric_limits<float>::denorm_min();
    Lower.Buffers.PositionValues[6][0] = std::numeric_limits<float>::max();
    Lower.Sections[0].Shadow = true;
    Lower.Sections[0].ForceOpaque = true;
    Lower.Sections[0].RayTracing = true;
    DistinctSource.Materials[1].UVInitialized = true;
    DistinctSource.Materials[1].UVOverride = true;
    DistinctSource.Materials[1].UVDensities = {{1.f, 2.f, 3.f, 4.f}};
    FP4PrivateMeshStage DistinctStage;
    FP4MeshStageReport DistinctReport;
    FString DistinctError;
    if (Check(TEXT("Different LOD triangle and UV channel counts preserve numbered material identity"),
            P4StagePrivateMeshDescriptions(
                DistinctSource, DistinctSlots, DistinctStage, DistinctReport, DistinctError)))
    {
        FStaticMeshAttributes A(*DistinctStage.LODs[1].Description);
        Check(TEXT("Lower LOD keeps all eight UV channels and four unused source vertices"),
            A.GetVertexInstanceUVs().GetNumIndices() == 8 && DistinctReport.Vertices == 14 &&
                DistinctReport.Corners == 12 && DistinctReport.UnusedVertices == 5 && DistinctReport.Sections == 3);
        const FName Imported = A.GetPolygonGroupMaterialSlotNames()[DistinctStage.LODs[1].Sections[0]];
        Check(TEXT("Imported FName number is retained by real native polygon group attributes"),
            Imported.GetNumber() == 43 && Imported.GetPlainNameString() == TEXT("NumberedImportedSlot") &&
                Imported.ToString() == DistinctSlots[1].ImportedSlot);
        Check(TEXT("Geometry staging cannot promote source material UV data or section flags to full asset fidelity"),
            !DistinctReport.EditableAssetQualified && !DistinctReport.DependenciesQualified &&
                !DistinctReport.OriginalDefaultsQualified &&
                DistinctReport.UnrepresentedFields.Contains(
                    TEXT("Section collision/shadow/opacity/ray-tracing flags and source vertex min/max")));
    }
    // Verify every original byte value survives UE's actual sRGB attribute pair.
    auto ColorSource = Fixture(false);
    ColorSource.LODs.resize(1);
    auto& B = ColorSource.LODs[0].Buffers;
    for (int32 Value = 0; Value < 256; ++Value)
    {
        B.PositionValues.push_back({{float(Value), 3, 4}});
        B.TangentValues.push_back(B.TangentValues[0]);
        for (auto& UV : B.UVValues)
            UV.push_back({{0, 1}});
        B.ColorRGBA.push_back({{uint8(Value), uint8(255 - Value), uint8(Value * 17), uint8(Value)}});
    }
    B.Vertices += 256;
    B.ColorVertices += 256;
    FP4PrivateMeshStage ColorStage;
    FP4MeshStageReport ColorReport;
    FString Error;
    Check(TEXT("All 256 sRGB/alpha byte values survive actual editor attributes"),
        P4StagePrivateMeshDescriptions(ColorSource, MaterialSlots, ColorStage, ColorReport, Error));
    auto WrongSlots = MaterialSlots;
    WrongSlots[0].ObjectIndex = -999;
    Check(TEXT("Wrong material source binding refuses"),
        !P4StagePrivateMeshDescriptions(ColorSource, WrongSlots, ColorStage, ColorReport, Error));
    WrongSlots = MaterialSlots;
    WrongSlots[0].ImportedSlot = FString::ChrN(NAME_SIZE, TCHAR('A'));
    Check(TEXT("Overlength FName refused before engine assertion"),
        !P4StagePrivateMeshDescriptions(ColorSource, WrongSlots, ColorStage, ColorReport, Error));
    WrongSlots = MaterialSlots;
    WrongSlots[0].ImportedSlot = FString(3, TEXT("A\0B"));
    Check(TEXT("Embedded NUL source label refuses rather than truncating"),
        !P4StagePrivateMeshDescriptions(ColorSource, WrongSlots, ColorStage, ColorReport, Error));
    auto InvalidName = ColorSource;
    InvalidName.Materials[0].Slot = std::string("\xff", 1);
    Check(TEXT("Invalid source UTF8 refuses exact text projection"),
        !P4StagePrivateMeshDescriptions(InvalidName, MaterialSlots, ColorStage, ColorReport, Error));
    return Good;
}
#include "Serialization/MemoryWriter.h"
#include "Serialization/NameAsStringProxyArchive.h"
#include "UObject/UObjectIterator.h"
namespace P4MeshNativeAttributeNext
{
TMap<FString, UObject*> AttributeNextNativeMeshes()
{
    TMap<FString, UObject*> Out;
    for (TObjectIterator<UObject> It; It; ++It)
        if (It->GetClass()->GetPathName() == TEXT("/Script/Engine.StaticMesh"))
            Out.Add(It->GetPathName(), *It);
    return Out;
}
bool AttributeNextSameMeshes(const TMap<FString, UObject*>& Before)
{
    const auto After = AttributeNextNativeMeshes();
    if (Before.Num() != After.Num())
        return false;
    for (const auto& Pair : Before)
    {
        const auto Current = After.Find(Pair.Key);
        if (!Current || *Current != Pair.Value)
            return false;
    }
    return true;
}
TArray<uint8> AttributeNextReportBytes(const FP4MeshStageReport& Report)
{
    // Snapshot the SAME initialized report object and backing storage, not
    // equality between independently allocated aggregates with padding.
    TArray<uint8> Out;
    Out.Append(reinterpret_cast<const uint8*>(&Report), int32(sizeof(Report)));
    if (Report.UnrepresentedFields.Num())
        Out.Append(reinterpret_cast<const uint8*>(Report.UnrepresentedFields.GetData()),
            int32(Report.UnrepresentedFields.Num() * sizeof(FString)));
    for (const FString& Text : Report.UnrepresentedFields)
        Out.Append(reinterpret_cast<const uint8*>(*Text), int32(Text.Len() * sizeof(TCHAR)));
    return Out;
}
bool AttributeNextDescriptionBytes(FAutomationTestBase& Test, const FP4PrivateMeshStage& Stage, TArray<uint8>& Out)
{
    Out.Reset();
    FMemoryWriter Writer(Out);
    FNameAsStringProxyArchive Archive(Writer);
    for (const auto& LOD : Stage.LODs)
    {
        if (!Test.TestTrue(TEXT("Owned native description exists before snapshot"), LOD.Description.IsValid()))
            return false;
        LOD.Description->Serialize(Archive);
    }
    return Test.TestTrue(TEXT("Independent real native description serialization succeeds"),
        !Archive.IsError() && !Writer.IsError() && Out.Num() > 0);
}
struct FAttributeNextFixture
{
    FAutomationTestBase& Test;
    bool Good = true;
    P4MapKit::FStaticMeshRenderPayload Source;
    TArray<FP4ExactMeshMaterialSlot> MaterialSlots;
    FP4PrivateMeshStage Baseline;
    FP4MeshStageReport Qualified;
    TArray<uint8> BaselineBytes;
    TMap<FString, UObject*> NativeMeshes;
    explicit FAttributeNextFixture(FAutomationTestBase& In)
        : Test(In), Source(Fixture(false)), MaterialSlots(Slots()), NativeMeshes(AttributeNextNativeMeshes())
    {}
    bool Check(const TCHAR* Label, bool Value)
    {
        Good = Test.TestTrue(Label, Value) && Good;
        return Value;
    }
    bool Begin()
    {
        FString Error;
        if (!Check(TEXT("Existing native authored two-LOD fixture stages exact geometry"),
                P4StagePrivateMeshDescriptions(Source, MaterialSlots, Baseline, Qualified, Error)) ||
            !Check(TEXT("Independent source denominator includes real unused attribute instances"),
                Qualified.GeometryAttributesVerified && Qualified.Vertices == 14 && Qualified.Corners == 18 &&
                    Qualified.UnusedVertices == 2 && Qualified.Sections == 4 && Baseline.LODs.Num() == 2) ||
            !Check(TEXT("No full-asset/default/dependency admission is inferred from private geometry"),
                !Qualified.EditableAssetQualified && !Qualified.OriginalDefaultsQualified &&
                    !Qualified.DependenciesQualified) ||
            !AttributeNextDescriptionBytes(Test, Baseline, BaselineBytes))
            return false;
        TArray<uint8> Again;
        return AttributeNextDescriptionBytes(Test, Baseline, Again) &&
            Check(TEXT("Independent native description snapshot is stable before any edit"), Again == BaselineBytes);
    }
    void Refuses(const TCHAR* Label, const TCHAR* Expected, const TFunction<void(FP4PrivateMeshStage&)>& Edit)
    {
        FP4PrivateMeshStage Candidate;
        FP4MeshStageReport Build;
        FString Error;
        if (!Check(TEXT("Fresh independent native candidate stages successfully before its real edit"),
                P4StagePrivateMeshDescriptions(Source, MaterialSlots, Candidate, Build, Error)))
            return;
        Edit(Candidate);
        TArray<uint8> EditedBytes, Again;
        if (!AttributeNextDescriptionBytes(Test, Candidate, EditedBytes))
        {
            Good = false;
            return;
        }
        if (!Check(TEXT("Actual owned native edit changes serialized candidate state"), EditedBytes != BaselineBytes))
            return;
        FP4MeshStageReport Report;
        Report.Vertices = 401;
        Report.Corners = 503;
        Report.UnusedVertices = 607;
        Report.Sections = 709;
        Report.UnrepresentedFields.Add(TEXT("retained caller-owned report"));
        const auto BeforeReport = AttributeNextReportBytes(Report);
        const FMeshDescription* const Pointer = Candidate.LODs[1].Description.Get();
        const bool Accepted = P4VerifyPrivateMeshDescriptions(Source, MaterialSlots, Candidate, Report, Error);
        Check(Label, !Accepted && Error == Expected);
        Check(TEXT("Failed native readback retains every caller report byte and no qualification"),
            AttributeNextReportBytes(Report) == BeforeReport && !Report.GeometryAttributesVerified &&
                !Report.EditableAssetQualified && !Report.OriginalDefaultsQualified && !Report.DependenciesQualified);
        if (!AttributeNextDescriptionBytes(Test, Candidate, Again))
        {
            Good = false;
            return;
        }
        Check(TEXT("Native refusal does not repair, replace or mutate the caller's edited descriptions"),
            Again == EditedBytes && Candidate.LODs[1].Description.Get() == Pointer);
        if (!AttributeNextDescriptionBytes(Test, Baseline, Again))
        {
            Good = false;
            return;
        }
        Check(TEXT("Previously qualified native descriptions remain completely unchanged"), Again == BaselineBytes);
        FP4MeshStageReport Rechecked;
        Check(
            TEXT("Original source/siblings/unused vertices still verify against the untouched qualified native stage"),
            P4VerifyPrivateMeshDescriptions(Source, MaterialSlots, Baseline, Rechecked, Error) &&
                Rechecked.GeometryAttributesVerified && Rechecked.Vertices == 14 && Rechecked.Corners == 18 &&
                Rechecked.UnusedVertices == 2);
        Check(TEXT("No native StaticMesh object publication or replacement occurred"),
            AttributeNextSameMeshes(NativeMeshes));
    }
};
} //namespace P4MeshNativeAttributeNext

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshNextNativeAttributeRefusals, "P4MapKit.Quality.MeshNextNativeAttributeRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshNextNativeAttributeRefusals::RunTest(const FString&)
{
    using namespace P4MeshNativeAttributeNext;
    FAttributeNextFixture F(*this);
    if (!F.Begin())
        return false;
    // Public native attribute removal on independently staged descriptions.
    // UV removal already exists in the current suite and is deliberately omitted.
    F.Refuses(TEXT("Missing native positions cannot be replaced by default coordinates"),
        TEXT("mesh-stage-readback-attribute-absent"), [](FP4PrivateMeshStage& S)
        { S.LODs[1].Description->VertexAttributes().UnregisterAttribute(MeshAttribute::Vertex::Position); });
    F.Refuses(TEXT("Missing native tangent cannot be regenerated during exact readback"),
        TEXT("mesh-stage-readback-attribute-absent"),
        [](FP4PrivateMeshStage& S)
        {
            S.LODs[1].Description->VertexInstanceAttributes().UnregisterAttribute(
                MeshAttribute::VertexInstance::Tangent);
        });
    F.Refuses(TEXT("Missing native normal cannot be regenerated during exact readback"),
        TEXT("mesh-stage-readback-attribute-absent"),
        [](FP4PrivateMeshStage& S)
        {
            S.LODs[1].Description->VertexInstanceAttributes().UnregisterAttribute(
                MeshAttribute::VertexInstance::Normal);
        });
    F.Refuses(TEXT("Missing native handedness cannot become an implicit positive sign"),
        TEXT("mesh-stage-readback-attribute-absent"),
        [](FP4PrivateMeshStage& S)
        {
            S.LODs[1].Description->VertexInstanceAttributes().UnregisterAttribute(
                MeshAttribute::VertexInstance::BinormalSign);
        });
    F.Refuses(TEXT("Missing native color attribute cannot become synthesized white"),
        TEXT("mesh-stage-readback-attribute-absent"),
        [](FP4PrivateMeshStage& S)
        {
            S.LODs[1].Description->VertexInstanceAttributes().UnregisterAttribute(MeshAttribute::VertexInstance::Color);
        });
    F.Refuses(TEXT("Missing native material-slot attribute cannot silently rebind sections"),
        TEXT("mesh-stage-readback-attribute-absent"),
        [](FP4PrivateMeshStage& S)
        {
            S.LODs[1].Description->PolygonGroupAttributes().UnregisterAttribute(
                MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
        });
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshNextNativeChannelRefusals, "P4MapKit.Quality.MeshNextNativeChannelRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshNextNativeChannelRefusals::RunTest(const FString&)
{
    using namespace P4MeshNativeAttributeNext;
    FAttributeNextFixture F(*this);
    if (!F.Begin())
        return false;
    // Change only the intended component. Earlier comparisons remain exact,
    // unlike a generic all-components color/UV drift already in the suite.
    F.Refuses(TEXT("Isolated red-byte drift refuses despite unchanged green/blue/alpha"),
        TEXT("mesh-stage-readback-color-loss"),
        [](FP4PrivateMeshStage& S)
        {
            FStaticMeshAttributes A(*S.LODs[1].Description);
            auto& C = A.GetVertexInstanceColors()[S.LODs[1].Corners[0]];
            C.X = FLinearColor::FromSRGBColor(FColor(17, 255, 1, 0)).R;
        });
    F.Refuses(TEXT("Isolated blue-byte drift refuses after red and green match"),
        TEXT("mesh-stage-readback-color-loss"),
        [](FP4PrivateMeshStage& S)
        {
            FStaticMeshAttributes A(*S.LODs[1].Description);
            auto& C = A.GetVertexInstanceColors()[S.LODs[1].Corners[0]];
            C.Z = FLinearColor::FromSRGBColor(FColor(0, 255, 33, 0)).B;
        });
    F.Refuses(TEXT("Isolated alpha-byte drift refuses after all RGB channels match"),
        TEXT("mesh-stage-readback-color-loss"),
        [](FP4PrivateMeshStage& S)
        {
            FStaticMeshAttributes A(*S.LODs[1].Description);
            A.GetVertexInstanceColors()[S.LODs[1].Corners[0]].W = .5f;
        });
    F.Refuses(TEXT("Isolated second UV coordinate drift refuses while signed-zero first coordinate survives"),
        TEXT("mesh-stage-readback-uv-loss"),
        [](FP4PrivateMeshStage& S)
        {
            FStaticMeshAttributes A(*S.LODs[1].Description);
            const auto I = S.LODs[1].Corners[0];
            const FVector2D Before = A.GetVertexInstanceUVs().Get(I, 0);
            A.GetVertexInstanceUVs().Set(I, 0, FVector2D(Before.X, .25f));
        });
    return F.Good && !HasAnyErrors();
}

#endif

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
// Private, uncompiled/unexecuted. Append after P4MeshNativeAttributeNext in
// P4StaticMeshStagingAutomation.cpp. Reuses the established authored native
// Fixture(false)/Slots() and full MeshDescription/report/native inventory oracles.
// Current258 missing source directions:68:9,90:46,93:68,129:36/55,15:9,27:13.
// Current258 missing native graph directions:185:13/55,186:13,203:21.
// Original201 already covered directions are deliberately not counted as new.
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Async/Async.h"
namespace P4MeshAfter289
{
using namespace P4MeshNativeAttributeNext;
template <typename T>
void After289VectorBytes(TArray<uint8>& Out, const std::vector<T>& Values)
{
    if (!Values.empty())
        Out.Append(reinterpret_cast<const uint8*>(Values.data()), int32(Values.size() * sizeof(T)));
}
TArray<uint8> After289SourceBytes(const P4MapKit::FStaticMeshRenderPayload& Source)
{
    // SAME initialized public source object/backing storage: padding is never
    // compared across independently allocated aggregates. Snapshot nested owned
    // buffers as well as vector/string headers to detect writes/reallocation.
    TArray<uint8> Out;
    Out.Append(reinterpret_cast<const uint8*>(&Source), int32(sizeof(Source)));
    After289VectorBytes(Out, Source.Materials);
    After289VectorBytes(Out, Source.LODs);
    After289VectorBytes(Out, Source.SocketIndices);
    After289VectorBytes(Out, Source.DistanceFields);
    for (const auto& D : Source.DistanceFields)
        After289VectorBytes(Out, D.CompressedVolume.Bytes);
    After289VectorBytes(Out, Source.OccluderVertices.Bytes);
    After289VectorBytes(Out, Source.OccluderIndices.Bytes);
    for (const auto& M : Source.Materials)
        if (!M.Slot.empty())
            Out.Append(reinterpret_cast<const uint8*>(M.Slot.data()), int32(M.Slot.size()));
    for (const auto& L : Source.LODs)
    {
        After289VectorBytes(Out, L.Sections);
        const auto& B = L.Buffers;
        After289VectorBytes(Out, B.PositionValues);
        After289VectorBytes(Out, B.TangentValues);
        After289VectorBytes(Out, B.ColorRGBA);
        After289VectorBytes(Out, B.UVValues);
        for (const auto& UV : B.UVValues)
            After289VectorBytes(Out, UV);
        for (const auto* A : {&B.Positions, &B.Tangents, &B.UV, &B.Colors, &B.RayTracing})
            After289VectorBytes(Out, A->Bytes);
        for (const auto* I : {&B.Main, &B.Reversed, &B.DepthOnly, &B.ReversedDepthOnly, &B.Wireframe, &B.Adjacency})
        {
            After289VectorBytes(Out, I->Data.Bytes);
            After289VectorBytes(Out, I->Indices);
        }
        After289VectorBytes(Out, B.Samplers);
        for (const auto& S : B.Samplers)
        {
            After289VectorBytes(Out, S.Probabilities);
            After289VectorBytes(Out, S.Aliases);
        }
    }
    return Out;
}
void After289SourceRefuses(FAttributeNextFixture& F, const TCHAR* Label, const TCHAR* Expected,
    const TFunction<void(P4MapKit::FStaticMeshRenderPayload&)>& Edit,
    const FP4MeshStageLimits& Limits = FP4MeshStageLimits{}, bool Worker = false)
{
    auto Source = F.Source;
    Edit(Source);
    const auto SourceBefore = After289SourceBytes(Source);
    FP4PrivateMeshStage Existing;
    FP4MeshStageReport Built;
    FString Error;
    if (!F.Check(TEXT("Previously valid independent native destination stages before source refusal"),
            P4StagePrivateMeshDescriptions(F.Source, F.MaterialSlots, Existing, Built, Error)))
        return;
    TArray<uint8> ExistingBefore, Again;
    if (!AttributeNextDescriptionBytes(F.Test, Existing, ExistingBefore))
    {
        F.Good = false;
        return;
    }
    if (!F.Check(TEXT("Previously valid native destination equals independently qualified baseline"),
            ExistingBefore == F.BaselineBytes))
        return;
    FP4MeshStageReport Report;
    Report.Vertices = 401;
    Report.Corners = 503;
    Report.UnusedVertices = 607;
    Report.Sections = 709;
    Report.UnrepresentedFields.Add(TEXT("retained caller report"));
    const auto ReportBefore = AttributeNextReportBytes(Report);
    const FMeshDescription* const First = Existing.LODs[0].Description.Get();
    const FMeshDescription* const Second = Existing.LODs[1].Description.Get();
    bool Accepted = false;
    if (Worker)
        Accepted = Async(EAsyncExecution::Thread,
            [&]()
            {
                return P4StagePrivateMeshDescriptions(Source, F.MaterialSlots, Existing, Report, Error, Limits);
            }).Get();
    else
        Accepted = P4StagePrivateMeshDescriptions(Source, F.MaterialSlots, Existing, Report, Error, Limits);
    F.Check(Label, !Accepted && Error == Expected);
    F.Check(TEXT("Refusal retains every initialized caller report byte and all unqualified flags"),
        AttributeNextReportBytes(Report) == ReportBefore && !Report.GeometryAttributesVerified &&
            !Report.EditableAssetQualified && !Report.DependenciesQualified && !Report.OriginalDefaultsQualified);
    F.Check(TEXT("Refusal cannot normalize, repair or rewrite any caller source field/storage/buffer"),
        After289SourceBytes(Source) == SourceBefore);
    if (!AttributeNextDescriptionBytes(F.Test, Existing, Again))
    {
        F.Good = false;
        return;
    }
    F.Check(TEXT("Full preexisting native destination bytes and both description identities survive refusal"),
        Again == ExistingBefore && Existing.LODs.Num() == 2 && Existing.LODs[0].Description.Get() == First &&
            Existing.LODs[1].Description.Get() == Second);
    FP4MeshStageReport Verified;
    F.Check(
        TEXT("Retained destination still independently verifies every baseline source corner/sibling/unused attribute"),
        P4VerifyPrivateMeshDescriptions(F.Source, F.MaterialSlots, Existing, Verified, Error) &&
            Verified.GeometryAttributesVerified && Verified.Vertices == 14 && Verified.Corners == 18 &&
            Verified.UnusedVertices == 2);
    if (!AttributeNextDescriptionBytes(F.Test, F.Baseline, Again))
    {
        F.Good = false;
        return;
    }
    F.Check(TEXT("Previously qualified independent baseline also remains byte-exact"), Again == F.BaselineBytes);
    F.Check(TEXT("No native StaticMesh publication/replacement accompanies source refusal"),
        AttributeNextSameMeshes(F.NativeMeshes));
}
} //namespace P4MeshAfter289
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshAfter289SourceBoundaries, "P4MapKit.Quality.MeshAfter289SourceBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshAfter289SourceBoundaries::RunTest(const FString&)
{
    using namespace P4MeshAfter289;
    FAttributeNextFixture F(*this);
    if (!F.Begin())
        return false;
    struct FCase
    {
        const TCHAR* Label;
        const TCHAR* Error;
        TFunction<void(P4MapKit::FStaticMeshRenderPayload&)> Edit;
    };
    const FCase Cases[] = {{TEXT("Explicit zero source vertex count cannot produce an empty replacement"),
                               TEXT("mesh-stage-lod-counts-invalid"),
                               [](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.Vertices = 0; }},
        {TEXT("Second source corner outside actual vertex inventory refuses after first corner remains valid"),
            TEXT("mesh-stage-index-or-repeated-corner-invalid"),
            [](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.Main.Indices[1] = 7; }},
        {TEXT("Third source corner outside actual vertex inventory refuses after first two remain valid"),
            TEXT("mesh-stage-index-or-repeated-corner-invalid"),
            [](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.Main.Indices[2] = 7; }},
        {TEXT("Declared low-precision source tangent below its signed8 width cannot be clamped"),
            TEXT("mesh-stage-packed-tangent-range"),
            [](P4MapKit::FStaticMeshRenderPayload& P) { P.LODs[1].Buffers.TangentValues[6][0] = -129; }},
        {TEXT("Oversized source material label refuses before native FName construction"),
            TEXT("mesh-stage-material-label-not-exactly-representable"),
            [](P4MapKit::FStaticMeshRenderPayload& P) { P.Materials[0].Slot = std::string(1025, 'A'); }}};
    for (const auto& Case : Cases)
        After289SourceRefuses(F, Case.Label, Case.Error, Case.Edit);
    FP4MeshStageLimits Limits;
    Limits.Corners = 8;
    After289SourceRefuses(
        F, TEXT("Caller corner budget smaller than the actual nine-corner LOD refuses before allocation"),
        TEXT("mesh-stage-lod-counts-invalid"), [](P4MapKit::FStaticMeshRenderPayload&) {}, Limits);
    return F.Good && !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshAfter289NativeGraphBoundaries,
    "P4MapKit.Quality.MeshAfter289NativeGraphBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshAfter289NativeGraphBoundaries::RunTest(const FString&)
{
    using namespace P4MeshAfter289;
    FAttributeNextFixture F(*this);
    if (!F.Begin())
        return false;
    F.Refuses(TEXT("Actual native polygon deletion cannot be filled with invented source geometry"),
        TEXT("mesh-stage-readback-count-loss"),
        [&F](FP4PrivateMeshStage& S)
        {
            auto& L = S.LODs[1];
            L.Description->DeletePolygon(L.Polygons[0]);
            F.Check(TEXT("Real deletion independently leaves seven vertices and two polygons/two triangles"),
                L.Description->Vertices().Num() == 7 && L.Description->Polygons().Num() == 2 &&
                    L.Description->Triangles().Num() == 2);
        });
    F.Refuses(
        TEXT(
            "Native quadrilateral keeps polygon/vertex/group counts but its second triangle must refuse exact source readback"),
        TEXT("mesh-stage-readback-count-loss"),
        [&F](FP4PrivateMeshStage& S)
        {
            auto& L = S.LODs[1];
            auto& D = *L.Description;
            const auto Polygon = L.Polygons[0];
            const auto Group = D.GetPolygonPolygonGroup(Polygon);
            // All four actual native attribute instances already exist. Reuse
            // the genuine unused vertex6 instead of adding an invented point.
            const TArray<FVertexInstanceID> Quad = {L.Corners[0], L.Corners[1], L.UnusedInstances[6], L.Corners[2]};
            D.DeletePolygon(Polygon);
            D.CreatePolygonWithID(Polygon, Group, Quad);
            F.Check(
                TEXT(
                    "Public quadrilateral edit independently retains vertex/polygon/group census and adds one native triangle"),
                D.Vertices().Num() == 7 && D.Polygons().Num() == 3 && D.PolygonGroups().Num() == 2 &&
                    D.Triangles().Num() == 4 && D.GetPolygonVertexInstances(Polygon).Num() == 4 &&
                    D.GetPolygonTriangleIDs(Polygon).Num() == 2);
        });
    F.Refuses(TEXT("Actual unused native polygon group cannot silently change the section census"),
        TEXT("mesh-stage-readback-count-loss"),
        [&F](FP4PrivateMeshStage& S)
        {
            auto& D = *S.LODs[1].Description;
            D.CreatePolygonGroup();
            F.Check(TEXT("Public empty group edit independently preserves geometry and changes only group census"),
                D.Vertices().Num() == 7 && D.Polygons().Num() == 3 && D.Triangles().Num() == 3 &&
                    D.PolygonGroups().Num() == 3);
        });
    F.Refuses(
        TEXT("Connecting the exact original unused source attribute instance cannot silently change source topology"),
        TEXT("mesh-stage-unused-vertex-loss"),
        [&F](FP4PrivateMeshStage& S)
        {
            auto& L = S.LODs[1];
            auto& D = *L.Description;
            const auto Polygon = L.Polygons[0];
            const auto Group = D.GetPolygonPolygonGroup(Polygon);
            // Public delete/recreate makes the native edge/triangle graph
            // coherent; no private element-array/map or source-ID corruption.
            const TArray<FVertexInstanceID> Corners = {L.UnusedInstances[6], L.Corners[1], L.Corners[2]};
            D.DeletePolygon(Polygon);
            D.CreatePolygonWithID(Polygon, Group, Corners);
            F.Check(
                TEXT(
                    "Coherent public topology edit preserves every native census and connects the genuine unused instance"),
                D.Vertices().Num() == 7 && D.Polygons().Num() == 3 && D.Triangles().Num() == 3 &&
                    D.PolygonGroups().Num() == 2 &&
                    D.GetVertexInstanceConnectedPolygons(L.UnusedInstances[6]).Num() == 1 &&
                    D.GetVertexInstanceVertex(L.UnusedInstances[6]) == L.Vertices[6]);
        });
    return F.Good && !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshAfter289WorkerRefusal, "P4MapKit.Quality.MeshAfter289WorkerRefusal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshAfter289WorkerRefusal::RunTest(const FString&)
{
    using namespace P4MeshAfter289;
    FAttributeNextFixture F(*this);
    if (!F.Begin())
        return false;
    After289SourceRefuses(
        F, TEXT("Real native worker call refuses before touching source/report/description state"),
        TEXT("mesh-stage-requires-editor-thread"), [](P4MapKit::FStaticMeshRenderPayload&) {}, FP4MeshStageLimits{},
        true);
    return F.Good && !HasAnyErrors();
}
#endif

#endif

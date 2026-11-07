#include "P4MaterialUsedTraceCache.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialUsedTraceCacheTest, "P4MapKit.Kadan.MaterialUsedTraceCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialUsedTraceCacheTest::RunTest(const FString&)
{
    namespace M = P4MapKit::Material;
    FP4MaterialUsedTraceCache Cache;
    const std::string Owner = "/Game/TraceCache/Master", Archive(64, 'a');
    bool Present = false, Throw = false;
    uint8 Byte = 1;
    int32 Traces = 0, Reads = 0;
    const M::MaterialOriginalImportRawResolver Read = [&](const std::string&, M::MaterialOriginalImportRawAsset& Out)
    {
        ++Reads;
        if (Throw)
            throw M::Refused("temporary read failure", "trace cache control");
        if (!Present)
            return false;
        Out.Header = std::make_shared<const std::vector<uint8>>(std::vector<uint8>{Byte});
        Out.Body = std::make_shared<const std::vector<uint8>>(std::vector<uint8>{2});
        return true;
    };
    const auto Trace = [&](const std::string&, const M::MaterialOriginalImportRawResolver& Resolve,
                           const M::MaterialOriginalArchiveAccess&)
    {
        ++Traces;
        M::FMaterialUsedResourceDiscovery Result;
        M::MaterialOriginalImportRawAsset Raw;
        try
        {
            const bool Found = Resolve("/Game/TraceCache/Dependency", Raw);
            Result.Status = Found ? M::EMaterialUsedResourceStatus::Complete
                                  : M::EMaterialUsedResourceStatus::Incomplete;
            Result.Refusal = Found ? std::to_string(Raw.Header->at(0)) : "missing dependency";
        }
        catch (const M::Refused&)
        {
            Result.Status = M::EMaterialUsedResourceStatus::Incomplete;
            Result.Refusal = "read failed";
        }
        return Result;
    };
    const auto Run = [&](const std::string& Hash)
    { return Cache.Discover(Owner, Read, M::MaterialOriginalArchiveAccess(), Hash, Trace); };
    TestTrue(TEXT("An absent dependency produces an incomplete trace"),
        Run(Archive).Status == M::EMaterialUsedResourceStatus::Incomplete);
    Run(Archive);
    TestEqual(TEXT("Repeated reference-only analysis is reused"), Traces, 1);
    TestEqual(TEXT("A cache hit still rechecks the missing source"), Reads, 2);
    Present = true;
    TestTrue(TEXT("A newly available dependency invalidates the incomplete trace"),
        Run(Archive).Status == M::EMaterialUsedResourceStatus::Complete);
    TestEqual(TEXT("Availability change retraces"), Traces, 2);
    Run(Archive);
    TestEqual(TEXT("A successful unchanged trace is also reused"), Traces, 2);
    Byte = 3;
    TestTrue(TEXT("Changed bytes of the same length change the result"), Run(Archive).Refusal == "3");
    TestEqual(TEXT("Same-length source mutation retraces"), Traces, 3);
    Run(std::string(64, 'b'));
    TestEqual(TEXT("A different held archive retraces"), Traces, 4);
    Present = false;
    Run(Archive);
    TestEqual(TEXT("The original absence witness can be reused when the source is absent again"), Traces, 4);
    Throw = true;
    Run(Archive);
    Run(Archive);
    TestEqual(TEXT("Throwing source reads never become cached incomplete results"), Traces, 6);
    Throw = false;
    Present = true;
    Run(Archive);
    TestEqual(TEXT("A temporary read failure does not poison a prior content witness"), Traces, 6);
    Run("");
    Run("");
    TestEqual(TEXT("An unbound archive never admits reuse"), Traces, 8);

    // A family recovery takes the owner graph a complete trace recovered, only for the same reads,
    // the same held archive and the same parameter collections.
    FP4MaterialUsedTraceCache Graphs;
    Present = true;
    Throw = false;
    Byte = 1;
    const auto GraphTrace = [&](const std::string&, const M::MaterialOriginalImportRawResolver& Resolve,
                                const M::MaterialOriginalArchiveAccess&)
    {
        M::FMaterialUsedResourceDiscovery Result;
        M::MaterialOriginalImportRawAsset Raw;
        const bool Found = Resolve("/Game/TraceCache/Dependency", Raw);
        Result.Status = Found ? M::EMaterialUsedResourceStatus::Complete : M::EMaterialUsedResourceStatus::Incomplete;
        if (Found)
        {
            Result.Graph = M::Json::MakeObject();
            Result.Graph["byte"] = M::Json(std::to_string(Raw.Header->at(0)));
            Result.CollectionPackages = {"/Game/TraceCache/CollectionB", "/Game/TraceCache/CollectionA"};
        }
        return Result;
    };
    Graphs.Discover(Owner, Read, M::MaterialOriginalArchiveAccess(), Archive, GraphTrace);
    M::Json Graph;
    TestTrue(TEXT("The traced owner graph is reused whatever order its collections come in"),
        Graphs.FindGraph(
            Owner, Read, Archive, {"/Game/TraceCache/CollectionA", "/Game/TraceCache/CollectionB"}, Graph) &&
            Graph.At("byte").Text() == "1");
    TestFalse(TEXT("Other parameter collections never reuse the graph"),
        Graphs.FindGraph(Owner, Read, Archive, {"/Game/TraceCache/CollectionA"}, Graph));
    TestFalse(TEXT("Another held archive never reuses the graph"),
        Graphs.FindGraph(Owner, Read, std::string(64, 'b'),
            {"/Game/TraceCache/CollectionA", "/Game/TraceCache/CollectionB"}, Graph));
    TestFalse(TEXT("Another owner never reuses the graph"),
        Graphs.FindGraph("/Game/TraceCache/Other", Read, Archive,
            {"/Game/TraceCache/CollectionA", "/Game/TraceCache/CollectionB"}, Graph));
    Byte = 4;
    TestFalse(TEXT("Changed source bytes never reuse the graph"),
        Graphs.FindGraph(
            Owner, Read, Archive, {"/Game/TraceCache/CollectionA", "/Game/TraceCache/CollectionB"}, Graph));
    Present = false;
    Graphs.Discover(Owner, Read, M::MaterialOriginalArchiveAccess(), Archive, GraphTrace);
    TestFalse(TEXT("An incomplete trace has no graph to reuse"), Graphs.FindGraph(Owner, Read, Archive, {}, Graph));
    TestEqual(TEXT("Only the one matching lookup counts as a reuse"), Graphs.GraphHits, 1);
    return !HasAnyErrors();
}
#endif

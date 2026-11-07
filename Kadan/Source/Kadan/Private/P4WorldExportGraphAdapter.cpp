#include "P4WorldExportGraph.h"
#include "P4AuthoringRows.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

FString P4WorldStreamingGraphJson(const FP4WorldStreamingGraphResult& Result)
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("format"), 1);
    Json->SetStringField(TEXT("source"), TEXT("native-editor-streaming"));
    Json->SetBoolField(TEXT("valid"), Result.IsValid());
    if (!Result.ErrorDetail.IsEmpty())
        Json->SetStringField(TEXT("error"), Result.ErrorDetail);
    if (Result.IsValid())
    {
        auto Graph = MakeShared<FJsonObject>();
        for (const auto& Pair : Result.Nodes)
        {
            auto Node = MakeShared<FJsonObject>();
            Node->SetStringField(TEXT("target"), Pair.Value.Target);
            TArray<TSharedPtr<FJsonValue>> Streams;
            for (const FP4WorldStreamingEntry& Entry : Pair.Value.Streams)
            {
                auto Row = MakeShared<FJsonObject>();
                Row->SetStringField(TEXT("stream_name"), Entry.StreamName);
                Row->SetStringField(TEXT("package"), Entry.Package);
                Row->SetStringField(TEXT("loaded_package"), Entry.LoadedPackage);
                Row->SetStringField(TEXT("package_to_load"), Entry.PackageToLoad);
                Row->SetBoolField(TEXT("loaded"), Entry.Loaded);
                Row->SetBoolField(TEXT("visible"), Entry.Visible);
                Row->SetBoolField(TEXT("editor_visible"), Entry.EditorVisible);
                Streams.Add(MakeShared<FJsonValueObject>(Row));
            }
            Node->SetArrayField(TEXT("streams"), Streams);
            Graph->SetObjectField(Pair.Key, Node);
        }
        Json->SetObjectField(TEXT("graph"), Graph);
        TArray<TSharedPtr<FJsonValue>> Unshipped;
        for (const FString& Package : Result.UnshippedStreams)
            Unshipped.Add(MakeShared<FJsonValueString>(Package));
        Json->SetArrayField(TEXT("unshipped_streams"), Unshipped);
    }
    FString Text;
    FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
    return Text;
}

FString UP4AuthoringTools::NativeWorldStreamingGraph(UWorld* World, const FString& TargetPackage)
{
    return P4WorldStreamingGraphJson(P4InspectWorldStreamingGraph(World, TargetPackage));
}

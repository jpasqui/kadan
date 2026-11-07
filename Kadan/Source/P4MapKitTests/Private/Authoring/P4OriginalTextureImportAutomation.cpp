#include "P4OriginalTextureImport.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalTextureImportProtocolTest, "P4MapKit.Native.OriginalTextureImportProtocol", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4OriginalTextureImportProtocolTest::RunTest(const FString&)
{
    const FP4OriginalTextureImportResult Thread = Async(EAsyncExecution::ThreadPool, []()
    {
        return P4ImportOriginalTextureNative(FString(), FString(), false);
    }).Get();
    TestTrue(TEXT("Native importer refuses off-thread use before file or UObject work"), Thread.Error == EP4OriginalTextureImportError::RequiresEditorThread);
    TestFalse(TEXT("Thread refusal cannot be mistaken for native success"), Thread.IsValid());
    TestNull(TEXT("Thread refusal exposes no asset"), Thread.Asset);
    TSharedPtr<FJsonObject> Json;
    TestTrue(TEXT("Legacy adapter emits a valid JSON object"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P4OriginalTextureImportResultJson(Thread)), Json) && Json.IsValid());
    if (Json)
    {
        TestFalse(TEXT("Legacy valid flag retains refusal"), Json->GetBoolField(TEXT("valid")));
        TestEqual(TEXT("Legacy refusal token preserved"), Json->GetStringField(TEXT("error")), FString(TEXT("texture-import-requires-editor-thread")));
        TestFalse(TEXT("Early refusal has no created asset"), Json->GetBoolField(TEXT("created")));
        TestFalse(TEXT("Early refusal has no replacement"), Json->GetBoolField(TEXT("replaced")));
        TestFalse(TEXT("Early refusal has no save"), Json->GetBoolField(TEXT("saved")));
        TestFalse(TEXT("Build report omitted before build"), Json->HasField(TEXT("class")));
        TestFalse(TEXT("Baseline omitted before source read"), Json->HasField(TEXT("source_sha256")));
        TestFalse(TEXT("Asset path omitted before destination admission"), Json->HasField(TEXT("asset")));
        TestFalse(TEXT("Mutation report omitted before mutation"), Json->HasField(TEXT("filled_standin")));
    }
    const FString AbsentHeader = FPaths::ProjectSavedDir() / TEXT("Kadan") /
        (TEXT("MissingTextureSource_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".uasset"));
    const FP4OriginalTextureImportResult Missing = P4ImportOriginalTextureNative(AbsentHeader, FString(), false);
    TestTrue(TEXT("Unreadable source has a native refusal enum"), Missing.Error == EP4OriginalTextureImportError::SourceUnreadable);
    TestEqual(TEXT("Unreadable source retains legacy message"), Missing.ErrorDetail, FString(TEXT("texture-import-source-unreadable")));
    TestFalse(TEXT("No build was attempted for unreadable source"), Missing.HasBuildReport);
    TestFalse(TEXT("No baseline invented for unreadable source"), Missing.Baseline.IsSet());
    return !HasAnyErrors();
}
#endif

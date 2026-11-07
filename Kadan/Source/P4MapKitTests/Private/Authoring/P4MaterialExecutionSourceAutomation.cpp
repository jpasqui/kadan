#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialUsedResources.h"
#include "P4KadanImport.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialExecutionSourceTest, "P4MapKit.Kadan.MaterialExecutionSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialExecutionSourceTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_EXECUTION_SOURCE")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Set P4_NATIVE_MATERIAL_EXECUTION_SOURCE=1 and exact PACKAGE/HEADER/BODY inputs for a current-read source-only control"));
        return true;
    }
    const FString Package = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_EXECUTION_PACKAGE"));
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_EXECUTION_HEADER"));
    const FString Body = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_EXECUTION_BODY"));
    if (Package.IsEmpty() || Header.IsEmpty() || Body.IsEmpty())
    {
        AddError(TEXT("Exact execution source package/header/body inputs are required"));
        return false;
    }
    auto Asset = MakeShared<FP4KadanSourceAsset>();
    Asset->Package = Package;
    Asset->Header = Header;
    Asset->Body = Body;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    Sources.Add(Asset);
    FP4KadanImportSourceScope Scope(Sources);
    FP4KadanSourceBinding Binding;
    FString Error;
    if (!TestTrue(TEXT("actual current source binding"), P4KadanReadBoundSource(Package, Binding, Error)))
    {
        AddError(Error);
        return false;
    }
    FP4MaterialExecutionSource Read;
    if (!TestTrue(TEXT("canonical material execution source"), P4ReadMaterialExecutionSource(Binding, Read, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("verified typed status"), Read.Status == EP4MaterialExecutionSourceRead::Verified);
    TestTrue(TEXT("current read witness"), P4MaterialExecutionSourceCurrent(Read, Error));
    if (Read.NativeClass == TEXT("/Script/Engine.Material"))
        for (const auto& Edge : Read.Resources)
            TestTrue(TEXT("master initial edges are typed collection prerequisites only"),
                Edge.Kind == EP4MaterialExecutionResourceKind::ParameterCollection);
    else
    {
        TestTrue(TEXT("actual MIC class"), Read.NativeClass == TEXT("/Script/Engine.MaterialInstanceConstant"));
        TestTrue(TEXT("MIC retains actual hard Parent"),
            Read.Resources.Num() > 0 && Read.Resources[0].Kind == EP4MaterialExecutionResourceKind::Parent);
    }
    for (const auto& Edge : Read.Resources)
    {
        TestTrue(TEXT("exact hard-import execution witness"),
            Edge.ImportIndex < 0 && !Edge.Property.IsEmpty() && !Edge.Object.IsEmpty() && !Edge.NativeClass.IsEmpty());
        if (Edge.Kind == EP4MaterialExecutionResourceKind::TextureOverride)
            TestTrue(TEXT("authored texture override only"),
                !Edge.Parameter.IsEmpty() && Edge.Property.StartsWith(TEXT("TextureParameterValues[")));
    }
    auto Edited = Read;
    Edited.NativeClass = TEXT("/Script/Engine.Texture2D");
    TestFalse(TEXT("changed execution witness rejected"), P4MaterialExecutionSourceCurrent(Edited, Error));
    auto Changed = Binding;
    Changed.SourceDigest = TEXT("0000000000000000000000000000000000000000000000000000000000000000");
    FP4MaterialExecutionSource Refused;
    TestFalse(TEXT("changed H+B snapshot refused"), P4ReadMaterialExecutionSource(Changed, Refused, Error));
    TestTrue(TEXT("changed H+B is InvalidSource, no error-string classification"),
        Refused.Status == EP4MaterialExecutionSourceRead::InvalidSource && Refused.Resources.Num() == 0);
    auto Wrong = Binding;
    Wrong.Package = Package + TEXT("_WrongSource");
    TestFalse(TEXT("wrong package source refused"), P4ReadMaterialExecutionSource(Wrong, Refused, Error));
    TestTrue(TEXT("wrong identity is InvalidSource"), Refused.Status == EP4MaterialExecutionSourceRead::InvalidSource);
    TestTrue(TEXT("real source and execution edges still current"), P4MaterialExecutionSourceCurrent(Read, Error));
    FP4KadanSourceBinding After;
    TestTrue(TEXT("unchanged exact source digest after controls"),
        P4KadanReadBoundSource(Package, After, Error) && After.SourceDigest == Binding.SourceDigest &&
            After.Header == Binding.Header && After.Body == Binding.Body);
    auto Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("passed"), !HasAnyErrors());
    Json->SetStringField(TEXT("package"), Package);
    Json->SetStringField(TEXT("native_class"), Read.NativeClass);
    Json->SetStringField(TEXT("binding_digest"), Binding.SourceDigest);
    Json->SetStringField(TEXT("source_sha256"), Read.SourceSha256);
    Json->SetNumberField(TEXT("initial_execution_edges"), Read.Resources.Num());
    Json->SetNumberField(TEXT("shader_graph_traces"), 0);
    Json->SetBoolField(TEXT("acquisition_enabled"), false);
    Json->SetBoolField(TEXT("uobject_admission"), false);
    FString Text;
    const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialUsedResources/execution-source.json");
    if (!FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text)) ||
        !IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) ||
        !FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        AddError(TEXT("Cannot retain material execution-source receipt"));
    return !HasAnyErrors();
}
#endif

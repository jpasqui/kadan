#include "P4ExportContainer.h"
#include "P4ExportProcess.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
namespace
{
// A tool that says it ran out of memory on each of its first Failures runs, and counts its runs.
FString MakeTool(const FString& Folder, int32 Failures)
{
    IFileManager::Get().DeleteDirectory(*Folder, false, true);
    IFileManager::Get().MakeDirectory(*Folder, true);
    const FString Tool = Folder / TEXT("tool.bat");
    FFileHelper::SaveStringToFile(FString::Printf(TEXT("@echo off\r\n"
                                                       "echo run>>\"%%~dp0runs\"\r\n"
                                                       "for /f %%%%n in ('type \"%%~dp0runs\" ^| find /c \"run\"') do set runs=%%%%n\r\n"
                                                       "if %%runs%% GTR %d exit /b 0\r\n"
                                                       "echo memory allocation of 1024 bytes failed\r\n"
                                                       "exit /b 1\r\n"),
                                      Failures),
        *Tool, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return FPaths::ConvertRelativePathToFull(Tool).Replace(TEXT("/"), TEXT("\\"));
}

int32 Runs(const FString& Folder)
{
    TArray<FString> Lines;
    FFileHelper::LoadFileToStringArray(Lines, *(Folder / TEXT("runs")));
    return Lines.Num();
}

const TCHAR* const Shell = TEXT("C:\\Windows\\System32\\cmd.exe");
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ContainerToolMemory, "P4MapKit.Kadan.ContainerToolOutOfMemory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ContainerToolMemory::RunTest(const FString&)
{
    const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Kadan/ContainerToolMemory"));

    // Memory frees up: the second run works and nothing is left of the first one's error.
    {
        const FString Folder = Root / TEXT("recovers");
        const FString Tool = MakeTool(Folder, 1);
        TArray<uint8> Out;
        FString Error;
        TestTrue(TEXT("The tool works on its second run"), P4RunExportTool(Shell, {TEXT("/c"), Tool}, 60.0, Out, Error));
        TestEqual(TEXT("It ran twice"), Runs(Folder), 2);
        TestTrue(TEXT("No error is left"), Error.IsEmpty());
    }
    // Memory never frees up: the error says so.
    {
        const FString Folder = Root / TEXT("never");
        const FString Tool = MakeTool(Folder, 100);
        TArray<uint8> Out;
        FString Error;
        TestFalse(TEXT("The tool fails"), P4RunExportTool(Shell, {TEXT("/c"), Tool}, 60.0, Out, Error));
        TestEqual(TEXT("It gets four runs"), Runs(Folder), 4);
        TestTrue(TEXT("The error names memory"), P4ToolRanOutOfMemory(Error));
    }
    // A stream has passed its bytes on and its output is never shown: it is not run again, and the error still names memory.
    {
        const FString Folder = Root / TEXT("stream");
        const FString Tool = MakeTool(Folder, 100);
        FString Error;
        const TFunction<bool(const uint8*, int32, FString&)> Sink = [](const uint8*, int32, FString&) { return true; };
        TestFalse(TEXT("The stream fails"), P4RunExportToolStream(Shell, {TEXT("/c"), Tool}, 60.0, Sink, 1024 * 1024, Error));
        TestEqual(TEXT("It runs once"), Runs(Folder), 1);
        TestTrue(TEXT("The error names memory"), P4ToolRanOutOfMemory(Error));
    }
    // Another failure is not memory: it is not retried or described as memory.
    {
        const FString Folder = Root / TEXT("other");
        IFileManager::Get().MakeDirectory(*Folder, true);
        TArray<uint8> Out;
        FString Error;
        TestFalse(TEXT("The tool fails"), P4RunExportTool(Shell, {TEXT("/c"), TEXT("exit 3")}, 60.0, Out, Error));
        TestFalse(TEXT("Not described as memory"), P4ToolRanOutOfMemory(Error));
    }
    IFileManager::Get().DeleteDirectory(*Root, false, true);
    return true;
}
#endif

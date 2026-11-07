#include "P4KadanOriginalConversion.h"
#include "P4KadanImport.h"
#include "P4ExportProcess.h"
#include "Misc/Paths.h"
#include "P4GameBuild.h"
bool P4KadanConvertMountedOriginal(
    const FP4KadanImportSettings& Settings, const FString& MountedHeader, const FString& Into, FString& Error)
{
    TArray<uint8> Output;
    // Keep the existing converter argv/order/timeout identical.
    return P4RunExportTool(FPaths::ConvertRelativePathToFull(Settings.Retoc),
        {TEXT("to-legacy"), TEXT("--no-shaders"), TEXT("--no-script-objects"), TEXT("--version"),
            P4GameEngineFor(Settings.GamePaks), TEXT("--filter"), TEXT("../../../") + MountedHeader, FPaths::ConvertRelativePathToFull(Settings.GamePaks),
            Into},
        600, Output, Error);
}
bool P4KadanConvertMountedOriginals(
    const FP4KadanImportSettings& Settings, const TArray<FString>& MountedHeaders, const FString& Into, FString& Error)
{
    // Each retoc run is mostly its start (reading the container index), so one run converts
    // many packages. Chunks keep the command line well under the Windows limit.
    const int32 LineBudget = 24000;
    int32 Next = 0;
    while (Next < MountedHeaders.Num())
    {
        TArray<FString> Args = {
            TEXT("to-legacy"), TEXT("--no-shaders"), TEXT("--no-script-objects"), TEXT("--version"),
            P4GameEngineFor(Settings.GamePaks)};
        int32 Length = 0;
        while (
            Next < MountedHeaders.Num() && (Args.Num() == 5 || Length + MountedHeaders[Next].Len() + 24 < LineBudget))
        {
            Args.Add(TEXT("--filter"));
            Args.Add(TEXT("../../../") + MountedHeaders[Next]);
            Length += MountedHeaders[Next].Len() + 24;
            ++Next;
        }
        Args.Add(FPaths::ConvertRelativePathToFull(Settings.GamePaks));
        Args.Add(Into);
        TArray<uint8> Output;
        if (!P4RunExportTool(FPaths::ConvertRelativePathToFull(Settings.Retoc), Args, 600, Output, Error))
            return false;
    }
    return true;
}

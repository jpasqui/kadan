// PRIVATE TEST HARNESS ONLY. Explicit coverage exclusion + compiled-input binding required.
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Editor/EditorEngine.h"
#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "Performance/MaxTickRateHandlerModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/UnrealTemplate.h"

namespace P4PrivateAutomationPacing
{
constexpr double Period = 1.0 / 60.0;
struct FHost
{
    FString Project, Marker, Owner, Purpose;
};

bool ReadBoundedJson(const FString& Path, int64 Limit, TSharedPtr<FJsonObject>& Out)
{
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (Size <= 0 || Size > Limit)
        return false;
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Path))
        return false;
    const auto Reader = TJsonReaderFactory<>::Create(Text);
    return FJsonSerializer::Deserialize(Reader, Out) && Out.IsValid();
}

bool ValidateHost(FHost& Out)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_AUTOMATION_PACING")) != TEXT("60") ||
        !FPaths::IsProjectFilePathSet() || IsRunningCommandlet() || !GIsEditor)
        return false;
    FString Actual = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    FString Authorized = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_AUTOMATION_HOST"));
    if (Authorized.IsEmpty() || FPaths::IsRelative(Authorized))
        return false;
    Authorized = FPaths::ConvertRelativePathToFull(Authorized);
    FPaths::NormalizeFilename(Actual);
    FPaths::NormalizeFilename(Authorized);
    if (!FPaths::CollapseRelativeDirectories(Actual) || !FPaths::CollapseRelativeDirectories(Authorized) ||
        !Actual.Equals(Authorized, ESearchCase::CaseSensitive) ||
        !Actual.EndsWith(TEXT(".uproject"), ESearchCase::CaseSensitive))
        return false;
    const FString Directory = FPaths::GetPath(Actual);
    FHost Host;
    Host.Project = Actual;
    Host.Marker = Directory / TEXT("AUTOMATION_HOST.json");
    Host.Owner = TEXT("p4mapkit-native-automation-host-v1");
    if (Directory.EndsWith(TEXT("/native-coverage-clean-host"), ESearchCase::CaseSensitive))
    {
    }
    else if (Directory.EndsWith(TEXT("/editor-coverage/automation-host"), ESearchCase::CaseSensitive))
        Host.Purpose = TEXT("isolated-native-automation-coverage");
    else
        return false;
    TSharedPtr<FJsonObject> Marker, Project;
    FString Owner;
    if (!ReadBoundedJson(Host.Marker, 4096, Marker) || !Marker->TryGetStringField(TEXT("owner"), Owner) ||
        !Owner.Equals(Host.Owner, ESearchCase::CaseSensitive))
        return false;
    if (!Host.Purpose.IsEmpty())
    {
        FString Purpose;
        if (!Marker->TryGetStringField(TEXT("purpose"), Purpose) ||
            !Purpose.Equals(Host.Purpose, ESearchCase::CaseSensitive))
            return false;
    }
    double Version = 0;
    FString Association;
    const TArray<TSharedPtr<FJsonValue>>* Plugins = nullptr;
    if (!ReadBoundedJson(Actual, 65536, Project) || !Project->TryGetNumberField(TEXT("FileVersion"), Version) ||
        Version != 3 || !Project->TryGetStringField(TEXT("EngineAssociation"), Association) ||
        Association != TEXT("4.26") || !Project->TryGetArrayField(TEXT("Plugins"), Plugins) || !Plugins)
        return false;
    const TCHAR* Required[] = {
        TEXT("P4Stubs"), TEXT("PythonScriptPlugin"), TEXT("EditorScriptingUtilities"), TEXT("Kadan")};
    bool Found[UE_ARRAY_COUNT(Required)] = {};
    for (const auto& Value : *Plugins)
    {
        const TSharedPtr<FJsonObject>* Plugin = nullptr;
        FString Name;
        bool Enabled = false;
        if (!Value.IsValid() || !Value->TryGetObject(Plugin) || !Plugin || !Plugin->IsValid() ||
            !(*Plugin)->TryGetStringField(TEXT("Name"), Name))
            return false;
        for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Required)); ++Index)
        {
            if (!Name.Equals(Required[Index], ESearchCase::CaseSensitive))
                continue;
            if (Found[Index] || !(*Plugin)->TryGetBoolField(TEXT("Enabled"), Enabled) || !Enabled)
                return false;
            Found[Index] = true;
        }
    }
    for (bool Present : Found)
        if (!Present)
            return false;
    Out = MoveTemp(Host);
    return true;
}

class FHandler final : public IMaxTickRateHandlerModule
{
public:
    explicit FHandler(const FHost& InHost) : Host(InHost) {}
    virtual void Initialize() override { bEnabled = true; }
    virtual void SetEnabled(bool Enabled) override { bEnabled = Enabled; }
    virtual bool GetEnabled() override { return bEnabled; }
    virtual void SetFlags(uint32 InFlags) override { Flags = InFlags; }
    virtual uint32 GetFlags() override { return Flags; }

    virtual bool HandleMaxTickRate(float DesiredMaxTickRate) override
    {
        // These are authentic engine states, never modified by this harness.
        if (!bEnabled || !IsInGameThread() || !GIsEditor || IsRunningCommandlet() || !GEditor || GEngine != GEditor ||
            GEngine->bUseFixedFrameRate || GEngine->GetCustomTimeStep() || FApp::IsBenchmarking() ||
            FApp::UseFixedTimeStep() || DesiredMaxTickRate != 3.0f || !GEditor->ShouldThrottleCPUUsage())
            return false;
        FString CurrentProject = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
        FPaths::NormalizeFilename(CurrentProject);
        if (!FPaths::CollapseRelativeDirectories(CurrentProject) ||
            !CurrentProject.Equals(Host.Project, ESearchCase::CaseSensitive))
            return false;
        auto& Features = IModularFeatures::Get();
        const FName Feature = GetModularFeatureName();
        if (Features.GetModularFeatureImplementationCount(Feature) != 1 ||
            Features.GetModularFeatureImplementation(Feature, 0) != this)
        {
            if (!bConflictLogged)
            {
                UE_LOG(LogTemp, Warning, TEXT("P4NativeAutomationPacing: competing handler; native pacing retained"));
                bConflictLogged = true;
            }
            return false;
        }
        const double Now = FPlatformTime::Seconds();
        const double Deadline = LastHandledEnd > 0 ? LastHandledEnd + Period : Now + Period;
        // Real elapsed wall time only. Sleep(0) yields if ordinary engine work
        // already consumed the full budget; default 3Hz sleeping stays handled.
        FPlatformProcess::SleepNoStats(static_cast<float>(FMath::Max(Deadline - Now, 0.0)));
        // Windows truncates coarse sleep durations to integer milliseconds.
        // Preserve the fractional budget using actual time and ordinary yields.
        while (FPlatformTime::Seconds() < Deadline)
            FPlatformProcess::SleepNoStats(0);
        const double End = FPlatformTime::Seconds();
        if (LastHandledEnd > 0)
        {
            const double Interval = End - LastHandledEnd;
            if (Intervals == 0)
                Minimum = Maximum = Interval;
            else
            {
                Minimum = FMath::Min(Minimum, Interval);
                Maximum = FMath::Max(Maximum, Interval);
            }
            Total += Interval;
            ++Intervals;
        }
        if (Handled == 0)
            UE_LOG(LogTemp, Display,
                TEXT(
                    "P4NativeAutomationPacing: first actual background 3Hz wait handled; real target60Hz; engine=%s; host=%s"),
                *GEditor->GetClass()->GetPathName(), *Host.Project);
        LastHandledEnd = End;
        ++Handled;
        return true;
    }
    void Summary() const
    {
        UE_LOG(LogTemp, Display,
            TEXT(
                "P4NativeAutomationPacing: stopped; handled=%llu; intervals=%llu; real_interval_min=%.6f; real_interval_mean=%.6f; real_interval_max=%.6f; host=%s"),
            static_cast<unsigned long long>(Handled), static_cast<unsigned long long>(Intervals), Minimum,
            Intervals ? Total / static_cast<double>(Intervals) : 0.0, Maximum, *Host.Project);
    }

private:
    FHost Host;
    bool bEnabled = false, bConflictLogged = false;
    uint32 Flags = 0;
    uint64 Handled = 0, Intervals = 0;
    double LastHandledEnd = 0, Minimum = 0, Maximum = 0, Total = 0;
};

// No global constructor, CRT or DllMain registrar. Explicit module lifecycle owns allocation.
FHandler* Handler = nullptr;
bool bStopping = false;
} //namespace P4PrivateAutomationPacing

void P4StartPrivateNativeAutomationPacing()
{
    using namespace P4PrivateAutomationPacing;
    if (Handler || bStopping || !IsInGameThread())
        return;
    const FString OptIn = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_AUTOMATION_PACING"));
    if (OptIn.IsEmpty())
        return;
    FHost Host;
    if (!ValidateHost(Host))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("P4NativeAutomationPacing: explicit request refused; exact owned host and value60 required"));
        return;
    }
    auto& Features = IModularFeatures::Get();
    const FName Feature = IMaxTickRateHandlerModule::GetModularFeatureName();
    if (Features.GetModularFeatureImplementationCount(Feature) != 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("P4NativeAutomationPacing: pre-existing handler; native pacing retained"));
        return;
    }
    Handler = new FHandler(Host);
    Handler->Initialize();
    Features.RegisterModularFeature(Feature, Handler);
    // Registration broadcasts synchronously and may invoke Stop. Inspect only
    // the current live owned pointer, never a captured possibly freed object.
    if (!Handler || !Handler->GetEnabled() || Features.GetModularFeatureImplementationCount(Feature) != 1 ||
        Features.GetModularFeatureImplementation(Feature, 0) != Handler)
        return;
    UE_LOG(LogTemp, Display,
        TEXT("P4NativeAutomationPacing: enabled owned opt-in; real target60Hz; owner=%s; marker=%s; host=%s"),
        *Host.Owner, *Host.Marker, *Host.Project);
}

void P4StopPrivateNativeAutomationPacing()
{
    using namespace P4PrivateAutomationPacing;
    if (!Handler)
        return;
    FHandler* Owned = Handler;
    Handler = nullptr;
    TGuardValue<bool> Stopping(bStopping, true);
    Owned->SetEnabled(false);
    IModularFeatures::Get().UnregisterModularFeature(IMaxTickRateHandlerModule::GetModularFeatureName(), Owned);
    Owned->Summary();
    delete Owned;
}
#endif

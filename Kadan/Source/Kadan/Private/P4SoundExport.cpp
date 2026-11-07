#include "P4SoundExport.h"
#include "P4ExportDriver.h"
#include "P4ExportProcess.h"
#include "P4MapKitCore/wwise_inventory.h"
#include "Audio.h"
#include "Sound/SoundWave.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace
{
bool StockSoundInventory(const FP4ExportModRequest& Request, P4MapKit::WwisePCM::Inventory& Out, FString& Error)
{
    TArray<FString> Paks;
    IFileManager::Get().FindFiles(Paks, *(Request.GamePaksDirectory / TEXT("*.pak")), true, false);
    Paks.Sort();
    try
    {
        for (const FString& Pak : Paks)
        {
            TArray<uint8> Bytes;
            const FString File = Request.GamePaksDirectory / Pak;
            if (!P4RunExportTool(Request.Tools.Repak, {TEXT("list"), File}, Request.Tools.TimeoutSeconds, Bytes, Error))
                return false;
            for (uint8 C : Bytes)
                if (!C)
                {
                    Error = TEXT("The game pak's file list contains binary data.");
                    return false;
                }
            FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
            TArray<FString> Files;
            FString(Converted.Length(), Converted.Get()).ParseIntoArrayLines(Files);
            for (const FString& Path : Files)
            {
                if (Path.EndsWith(TEXT(".bnk"), ESearchCase::IgnoreCase))
                {
                    if (Out.Banks >= 4096)
                    {
                        Error = TEXT("The game inputs contain too many sound banks.");
                        return false;
                    }
                    if (!P4RunExportTool(
                            Request.Tools.Repak, {TEXT("get"), File, Path}, Request.Tools.TimeoutSeconds, Bytes, Error))
                        return false;
                    P4MapKit::WwisePCM::ReadBankInventory({Bytes.GetData(), Bytes.GetData() + Bytes.Num()},
                        FPaths::GetCleanFilename(Path).Equals(TEXT("Init.bnk"), ESearchCase::IgnoreCase), Out);
                }
                else if (Path.EndsWith(TEXT(".wem"), ESearchCase::IgnoreCase))
                {
                    const FString Name = FPaths::GetBaseFilename(Path);
                    uint64 Id = 0;
                    bool Numeric = !Name.IsEmpty();
                    for (TCHAR C : Name)
                    {
                        if (C < TEXT('0') || C > TEXT('9') || Id > (MAX_uint32 - uint32(C - TEXT('0'))) / 10)
                        {
                            Numeric = false;
                            break;
                        }
                        Id = Id * 10 + uint32(C - TEXT('0'));
                    }
                    if (Numeric && Id)
                        Out.Ids.insert(uint32(Id));
                }
            }
        }
        if (!Out.HasInit || !Out.Ids.count(P4MapKit::WwisePCM::Id("spatial")))
        {
            Error = TEXT("Read the game's Init bank and spatial bus before exporting sounds.");
            return false;
        }
    }
    catch (const std::exception& Why)
    {
        Error = UTF8_TO_TCHAR(Why.what());
        return false;
    }
    return true;
}
} //namespace

bool P4CreatorWaveSample(USoundWave* Wave, P4MapKit::WwisePCM::Sample& Out, FString& Error)
{
    if (!IsInGameThread() || !IsValid(Wave))
    {
        Error = TEXT("Choose an imported sound wave of your own.");
        return false;
    }
    TArray<uint8> Source;
    {
        FScopeLock Guard(&Wave->RawDataCriticalSection);
        const int64 Size = Wave->RawData.GetBulkDataSize();
        if (Size <= 0 || Size > 64 * 1024 * 1024)
        {
            Error = TEXT("Import a PCM16 WAV of at most 30 seconds in mono or stereo.");
            return false;
        }
        const void* Data = Wave->RawData.LockReadOnly();
        if (Data)
        {
            Source.SetNumUninitialized(int32(Size));
            FMemory::Memcpy(Source.GetData(), Data, Size);
        }
        Wave->RawData.Unlock();
        if (!Data)
        {
            Error = TEXT("The sound's imported WAV could not be read.");
            return false;
        }
    }
    FWaveModInfo Info;
    if (!Info.ReadWaveInfo(Source.GetData(), Source.Num(), &Error) || !Info.pFormatTag || *Info.pFormatTag != 1 ||
        !Info.pBitsPerSample || *Info.pBitsPerSample != 16 || !Info.pChannels || !Info.pSamplesPerSec ||
        !Info.pBlockAlign || *Info.pBlockAlign != *Info.pChannels * 2)
    {
        Error = TEXT("Import a PCM16 WAV in mono or stereo to export this sound.");
        return false;
    }
    P4MapKit::WwisePCM::Sample Sample;
    Sample.Label = TCHAR_TO_UTF8(*Wave->GetName().ToLower());
    Sample.Rate = *Info.pSamplesPerSec;
    Sample.Channels = *Info.pChannels;
    Sample.PCM.assign(Info.SampleDataStart, Info.SampleDataStart + Info.SampleDataSize);
    try
    {
        if (!P4MapKit::WwisePCM::Label(Sample.Label))
            throw std::runtime_error("Name the sound with at most 48 lowercase letters, digits or underscores.");
        P4MapKit::WwisePCM::Media(Sample);
    }
    catch (const std::exception& Why)
    {
        Error = UTF8_TO_TCHAR(Why.what());
        return false;
    }
    Out = std::move(Sample);
    Error.Reset();
    return true;
}

bool P4ExportCreatorSoundBank(const FP4ExportModRequest& Request, const TArray<USoundWave*>& Sounds,
    const FString& Metadata, FP4ExportModResult& Result, FString& Error)
{
    if (!Sounds.Num())
        return true;
    if (Sounds.Num() > 64)
    {
        Error = TEXT("Choose at most 64 sounds for this mod's sound bank.");
        return false;
    }
    std::vector<P4MapKit::WwisePCM::Sample> Samples;
    for (USoundWave* Sound : Sounds)
    {
        P4MapKit::WwisePCM::Sample Sample;
        if (!P4CreatorWaveSample(Sound, Sample, Error))
        {
            Error = Sound->GetName() + TEXT(": ") + Error;
            return false;
        }
        Samples.push_back(std::move(Sample));
    }
    P4MapKit::WwisePCM::Inventory Stock;
    if (!StockSoundInventory(Request, Stock, Error))
        return false;
    try
    {
        const auto Bank = P4MapKit::WwisePCM::Build(Samples, TCHAR_TO_UTF8(*(Request.Mod.Id + TEXT(".sounds"))),
            Stock.Project, P4MapKit::WwisePCM::Id("spatial"), Stock.Ids);
        TArray<uint8> Bytes;
        Bytes.Append(Bank.Data.data(), int32(Bank.Data.size()));
        if (!IFileManager::Get().MakeDirectory(*Metadata, true) ||
            !FFileHelper::SaveArrayToFile(Bytes, *(Metadata / TEXT("sounds.bnk"))))
        {
            Error = TEXT("The mod's sound bank could not be written.");
            return false;
        }
        for (const auto& Event : Bank.Events)
            Result.Told.Add(FString::Printf(TEXT("The sound %s is event #%u in %s:sounds."),
                UTF8_TO_TCHAR(Event.Label.c_str()), Event.EventId, *Request.Mod.Id));
    }
    catch (const std::exception& Why)
    {
        Error = UTF8_TO_TCHAR(Why.what());
        return false;
    }
    return true;
}

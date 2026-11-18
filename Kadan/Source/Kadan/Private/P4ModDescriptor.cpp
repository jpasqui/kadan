#include "P4ModDescriptor.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4AuthoringRows.h"
#include "P4NativeHash.h"
#include "P4MapKitCore/export_contract.h"
#include "P4ExportPublication.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
P4MapKit::Export::Descriptor Native(const FP4ModDescriptor& D)
{
    P4MapKit::Export::Descriptor Result;
    Result.Format = D.Format;
    Result.Id = TCHAR_TO_UTF8(*D.Id);
    Result.Name = TCHAR_TO_UTF8(*D.Name);
    Result.Version = TCHAR_TO_UTF8(*D.Version);
    return Result;
}

bool Same(const FP4ModDescriptor& A, const FP4ModDescriptor& B)
{
    return A.Format == B.Format && P4ExactNativeString(A.Id, B.Id) && P4ExactNativeString(A.Name, B.Name) &&
        P4ExactNativeString(A.Version, B.Version);
}

bool Path(const FString& Directory, const FString& Id, FString& Out, FString& Error)
{
    FP4ModDescriptor D;
    D.Id = Id;
    D.Name = TEXT("Identity");
    D.Version = TEXT("0.0.0");
    if (!P4ValidateModDescriptor(D, Error))
        return false;
    bool HasNul = false;
    for (TCHAR C : Directory)
        if (C == 0)
            HasNul = true;
    if (Directory.IsEmpty() || HasNul)
    {
        Error = TEXT("A project directory is required");
        return false;
    }
    Out = FPaths::Combine(Directory, TEXT("Config/KadanMods"), Id + TEXT(".json"));
    return true;
}

bool Parse(const FString& Text, FP4ModDescriptor& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Object;
    if (!P4ParseJson(Text, Object) || Object->Values.Num() != 4)
    {
        Error = TEXT("Mod descriptor must contain exactly format, id, name and version");
        return false;
    }
    double Format = 0;
    FP4ModDescriptor Candidate;
    const TSharedPtr<FJsonValue>* FormatValue = Object->Values.Find(TEXT("format"));
    if (!FormatValue || !FormatValue->IsValid() || (*FormatValue)->Type != EJson::Number ||
        !Object->TryGetNumberField(TEXT("format"), Format) || Format != 1 ||
        !Object->TryGetStringField(TEXT("id"), Candidate.Id) ||
        !Object->TryGetStringField(TEXT("name"), Candidate.Name) ||
        !Object->TryGetStringField(TEXT("version"), Candidate.Version))
    {
        Error = TEXT("Mod descriptor fields have invalid types or format");
        return false;
    }
    if (!P4ValidateModDescriptor(Candidate, Error))
        return false;
    Out = MoveTemp(Candidate);
    return true;
}
} //namespace

bool P4ValidateModDescriptor(const FP4ModDescriptor& D, FString& Error)
{
    // Reject embedded NUL before the UTF-8 conversion used by the portable contract.
    for (const FString* Value : {&D.Id, &D.Name, &D.Version})
        for (TCHAR C : *Value)
            if (C == 0)
            {
                Error = TEXT("Mod descriptor contains an embedded NUL");
                return false;
            }
    std::string Why;
    if (!P4MapKit::Export::Validate(Native(D), Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    Error.Reset();
    return true;
}

FString UP4AuthoringTools::NativeSaveMod(
    const FString& ModId, const FString& Name, const FString& Version, bool UpdateExisting)
{
    FP4ModDescriptor Value;
    Value.Id = ModId;
    Value.Name = Name;
    Value.Version = Version;
    FP4ModDescriptor Previous;
    FString Error;
    const bool Existing = UpdateExisting && P4ReadModDescriptor(FPaths::ProjectDir(), ModId, Previous, Error);
    const bool Saved = Existing && Same(Value, Previous)
        ? true
        : P4WriteModDescriptor(FPaths::ProjectDir(), Value, Existing ? &Previous : nullptr, Error);
    if (Saved)
        Error.Reset();
    auto Object = MakeShared<FJsonObject>();
    Object->SetBoolField(TEXT("valid"), Saved);
    Object->SetStringField(TEXT("error"), Error);
    Object->SetNumberField(TEXT("format"), Value.Format);
    Object->SetStringField(TEXT("id"), Value.Id);
    Object->SetStringField(TEXT("name"), Value.Name);
    Object->SetStringField(TEXT("version"), Value.Version);
    FString Text;
    FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
    return Text;
}

bool P4ModDerivedNames(const FP4ModDescriptor& D, FP4ModNames& Out, FString& Error)
{
    if (!P4ValidateModDescriptor(D, Error))
        return false;
    P4MapKit::Export::Names Names;
    std::string Why;
    if (!P4MapKit::Export::DerivedNames(Native(D), Names, Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    FP4ModNames Candidate;
    Candidate.PackageRoot = UTF8_TO_TCHAR(Names.Root.c_str());
    Candidate.Container = UTF8_TO_TCHAR(Names.Container.c_str());
    Out = MoveTemp(Candidate);
    return true;
}

bool P4IsModContentPath(const FString& PackageRoot, const FString& Package)
{
    if (!P4IsContentPackage(Package))
        return false;
    if (!PackageRoot.IsEmpty() && Package.StartsWith(PackageRoot + TEXT("/"), ESearchCase::CaseSensitive))
        return true;
    // Kadan's live preview packages are its own and never ship. Any other folder, in /Game/Mods too, is the mod's.
    return !Package.StartsWith(TEXT(P4_PREVIEW_FOLDER "/"), ESearchCase::IgnoreCase);
}

FString P4ModContentPathIssue(const FString& PackageRoot, const FString& Package)
{
    if (P4IsModContentPath(PackageRoot, Package))
        return FString();
    if (!P4IsContentPackage(Package))
        return Package + TEXT(" is outside the project's content folder.");
    return FString::Printf(TEXT("%s is in Kadan's live preview folder, which no mod ships. Keep this mod's assets in %s."),
        *FPackageName::GetLongPackagePath(Package), PackageRoot.IsEmpty() ? TEXT("its own folder") : *PackageRoot);
}

bool P4ReadModDescriptor(const FString& Directory, const FString& Id, FP4ModDescriptor& Out, FString& Error)
{
    FString File, Text;
    FP4ModDescriptor Candidate;
    if (!Path(Directory, Id, File, Error))
        return false;
    const int64 Size = IFileManager::Get().FileSize(*File);
    if (Size < 0 || Size > 65536 || !FFileHelper::LoadFileToString(Text, *File))
    {
        Error = TEXT("Mod descriptor is missing, unreadable or exceeds 64 KiB");
        return false;
    }
    if (!Parse(Text, Candidate, Error))
        return false;
    if (!P4ExactNativeString(Id, Candidate.Id))
    {
        Error = TEXT("Mod descriptor identity differs from its filename");
        return false;
    }
    Out = MoveTemp(Candidate);
    return true;
}

bool P4ListModDescriptors(const FString& Directory, TArray<FP4ModDescriptor>& Out, FString& Error)
{
    if (Directory.IsEmpty())
    {
        Error = TEXT("A project directory is required");
        return false;
    }
    TArray<FString> Files;
    TArray<FP4ModDescriptor> Candidate;
    IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory, TEXT("Config/KadanMods/*.json")), true, false);
    Files.Sort();
    for (const FString& File : Files)
    {
        FP4ModDescriptor D;
        if (!P4ReadModDescriptor(Directory, FPaths::GetBaseFilename(File), D, Error))
            return false;
        Candidate.Add(MoveTemp(D));
    }
    Out = MoveTemp(Candidate);
    Error.Reset();
    return true;
}

bool P4WriteModDescriptor(
    const FString& Directory, const FP4ModDescriptor& D, const FP4ModDescriptor* Expected, FString& Error)
{
    FString File;
    if (!P4ValidateModDescriptor(D, Error) || !Path(Directory, D.Id, File, Error))
        return false;
    if (Expected && !P4ExactNativeString(D.Id, Expected->Id))
    {
        Error = TEXT("An existing mod identity cannot be changed");
        return false;
    }
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true))
    {
        Error = TEXT("Cannot create the mod descriptor directory");
        return false;
    }
    // Exclusive lock serializes editor instances without trusting an overwritten stale snapshot.
    const FString Lock = File + TEXT(".lock");
    TUniquePtr<FArchive> Guard = P4HoldLockFile(Lock);
    if (!Guard)
    {
        Error = TEXT("Another writer holds the mod descriptor lock");
        return false;
    }
    struct FRelease
    {
        TUniquePtr<FArchive>& Guard;
        FString File;
        ~FRelease()
        {
            Guard.Reset();
            IFileManager::Get().Delete(*File);
        }
    } Release{Guard, Lock};
    if (Expected)
    {
        FP4ModDescriptor Stored;
        if (!P4ReadModDescriptor(Directory, D.Id, Stored, Error))
            return false;
        if (!Same(Stored, *Expected))
        {
            Error = TEXT("The mod's settings changed since they were opened. Reload them before saving.");
            return false;
        }
    }
    else if (IFileManager::Get().FileExists(*File))
    {
        Error = TEXT("Mod identity already exists");
        return false;
    }
    auto Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("format"), 1);
    Object->SetStringField(TEXT("id"), D.Id);
    Object->SetStringField(TEXT("name"), D.Name);
    Object->SetStringField(TEXT("version"), D.Version);
    FString Text;
    FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
    const FString Temporary = File + TEXT(".tmp.") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !IFileManager::Get().Move(*File, *Temporary, Expected != nullptr, false, false, false))
    {
        IFileManager::Get().Delete(*Temporary);
        Error = TEXT("Cannot publish mod descriptor");
        return false;
    }
    Error.Reset();
    return true;
}

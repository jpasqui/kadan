#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4Common.h"
#include "P4AuthoringRows.h"
#include "P4TextureProfileCapture.h"
#include "P4NativeHash.h"
#include "P4MapKitCore/texture_profiles.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
// Whether a path, and every folder above it, is an ordinary one: not a link or a junction.
bool P4SourcePathWithoutReparse(const FString& Path)
{
#if PLATFORM_WINDOWS
    FString At = Path;
    while (!At.IsEmpty())
    {
        const DWORD Attributes = GetFileAttributesW(*At);
        if (Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        const FString Parent = FPaths::GetPath(At);
        if (Parent == At)
            break;
        At = Parent;
    }
    return true;
#else
    return false;
#endif
}
// Whether a folder is one a capture may be read from: a whole path, outside any Content folder,
// with no link on the way to it. Normalized is the path as it is compared.
bool P4PrivateSourceDirectory(const FString& Input, FString& Normalized)
{
    if (Input.IsEmpty() || FPaths::IsRelative(Input))
        return false;
    for (const TCHAR Character : Input)
        if (!Character)
            return false;
    Normalized = FPaths::ConvertRelativePathToFull(Input);
    FPaths::NormalizeFilename(Normalized);
    if (!FPaths::CollapseRelativeDirectories(Normalized))
        return false;
    if (Normalized.Contains(TEXT("/Content/"), ESearchCase::IgnoreCase) ||
        Normalized.EndsWith(TEXT("/Content"), ESearchCase::IgnoreCase))
        return false;
    return P4SourcePathWithoutReparse(Normalized);
}
struct FProfileSnapshot
{
    FString Path, Digest;
    std::string Bytes;
    int64 Limit = 0;
};
// Reject replacement decoding, embedded NUL, overlong encodings, surrogate
// codepoints and out-of-range Unicode before any engine string conversion.
bool ProfileUTF8(const std::string& Bytes)
{
    for (size_t At = 0; At < Bytes.size();)
    {
        const uint8 First = uint8(Bytes[At++]);
        if (!First)
            return false;
        if (First < 128)
            continue;
        uint32 Value = 0, Minimum = 0;
        size_t Tail = 0;
        if (First >= 0xc2 && First <= 0xdf)
        {
            Value = First & 31;
            Tail = 1;
            Minimum = 128;
        }
        else if (First >= 0xe0 && First <= 0xef)
        {
            Value = First & 15;
            Tail = 2;
            Minimum = 2048;
        }
        else if (First >= 0xf0 && First <= 0xf4)
        {
            Value = First & 7;
            Tail = 3;
            Minimum = 65536;
        }
        else
            return false;
        if (Tail > Bytes.size() - At)
            return false;
        for (size_t Index = 0; Index < Tail; ++Index)
        {
            const uint8 Next = uint8(Bytes[At++]);
            if ((Next & 0xc0) != 0x80)
                return false;
            Value = (Value << 6) | (Next & 63);
        }
        if (Value < Minimum || Value > 0x10ffff || (Value >= 0xd800 && Value <= 0xdfff) || Value == 0xfffe ||
            Value == 0xffff)
            return false;
    }
    return true;
}
bool ProfileSnapshot(const FString& Input, int64 Limit, FProfileSnapshot& Out)
{
    if (Input.IsEmpty() || FPaths::IsRelative(Input))
        return false;
    for (const TCHAR C : Input)
        if (!C)
            return false;
    FProfileSnapshot Local;
    Local.Path = FPaths::ConvertRelativePathToFull(Input);
    FPaths::NormalizeFilename(Local.Path);
    Local.Limit = Limit;
    FString Parent;
    if (!FPaths::CollapseRelativeDirectories(Local.Path) ||
        !P4PrivateSourceDirectory(FPaths::GetPath(Local.Path), Parent) || !P4SourcePathWithoutReparse(Local.Path))
        return false;
    int64 BeforeSize = 0;
    FString BeforeHash;
    if (!P4HashFile(Local.Path, Limit, BeforeSize, BeforeHash) || BeforeSize <= 0)
        return false;
    TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Local.Path));
    if (!File || File->TotalSize() != BeforeSize)
        return false;
    Local.Bytes.resize(size_t(BeforeSize));
    File->Serialize(&Local.Bytes[0], BeforeSize);
    if (File->IsError() || File->TotalSize() != BeforeSize)
        return false;
    File.Reset();
    if (!ProfileUTF8(Local.Bytes) ||
        !P4HashByteStream(
            [&](const TFunction<bool(const uint8*, int32)>& Update)
            {
                for (size_t At = 0; At < Local.Bytes.size(); At += 65536)
                    if (!Update(reinterpret_cast<const uint8*>(Local.Bytes.data() + At),
                            int32(FMath::Min<size_t>(65536, Local.Bytes.size() - At))))
                        return false;
                return true;
            },
            Local.Digest) ||
        Local.Digest != BeforeHash)
        return false;
    Out = MoveTemp(Local);
    return true;
}
bool ProfileUnchanged(const FProfileSnapshot& Source)
{
    int64 Size = 0;
    FString Digest;
    return P4SourcePathWithoutReparse(Source.Path) && P4HashFile(Source.Path, Source.Limit, Size, Digest) &&
        Size == int64(Source.Bytes.size()) && Digest == Source.Digest;
}
FString ProfileString(const std::string& Text)
{
    FUTF8ToTCHAR Converted(Text.data(), int32(Text.size()));
    return FString(Converted.Length(), Converted.Get());
}
TSharedPtr<FJsonObject> ProfileSourceJson(const FProfileSnapshot& Source)
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("sha256"), Source.Digest);
    Json->SetNumberField(TEXT("bytes"), double(Source.Bytes.size()));
    return Json;
}
FString ProfileResult(const TSharedPtr<FJsonObject>& Object)
{
    FString Json = P4CondensedJson(Object.ToSharedRef());
    return Json;
}
} //namespace

FString UP4AuthoringTools::NativeTextureProfileInventory(
    const FString& CaptureFile, const TArray<FString>& ConfigFiles, const TArray<FString>& ProfileSections)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("format"), 1);
    Result->SetBoolField(TEXT("valid"), false);
    Result->SetBoolField(TEXT("editable_import_qualified"), false);
    Result->SetBoolField(TEXT("effective_profile_qualified"), false);
    Result->SetBoolField(TEXT("original_defaults_qualified"), false);
    Result->SetBoolField(TEXT("runtime_provenance_authenticated"), false);
    Result->SetBoolField(TEXT("source_files_atomic"), false);
    Result->SetBoolField(TEXT("native_group_mapping_qualified"), false);
    const auto Reject = [&](const FString& Error)
    {
        Result->SetStringField(TEXT("error"), Error);
        return ProfileResult(Result);
    };
    if (!ConfigFiles.Num() || ConfigFiles.Num() > 16 || !ProfileSections.Num() || ProfileSections.Num() > 64)
        return Reject(TEXT("texture-profile-selection-budget"));
    FProfileSnapshot Capture;
    if (!ProfileSnapshot(CaptureFile, 128ll * 1024 * 1024, Capture))
        return Reject(TEXT("texture-profile-capture-snapshot"));
    std::string CaptureText = Capture.Bytes;
    if (CaptureText.compare(0, 3, "\xef\xbb\xbf") == 0)
        CaptureText.erase(0, 3);
    FP4TextureProfileCapture Enum;
    FString Error;
    if (!P4ReadTextureProfileCapture(ProfileString(CaptureText), {}, {}, Enum, Error))
        return Reject(Error);
    std::vector<P4MapKit::FTextureGroupRecord> Groups;
    for (const auto& Entry : Enum.Entries)
        Groups.push_back({TCHAR_TO_UTF8(*Entry.Name), Entry.Value});
    std::vector<std::string> Sections;
    for (const FString& Section : ProfileSections)
    {
        for (const TCHAR C : Section)
            if (!C)
                return Reject(TEXT("texture-profile-selection-nul"));
        FTCHARToUTF8 Converted(*Section, Section.Len());
        std::string UTF8(Converted.Get(), size_t(Converted.Length()));
        if (!ProfileUTF8(UTF8) || ProfileString(UTF8) != Section)
            return Reject(TEXT("texture-profile-selection-unicode"));
        Sections.push_back(MoveTemp(UTF8));
    }
    TArray<FProfileSnapshot> Configs;
    TArray<TSharedPtr<FJsonValue>> ProfileFiles;
    int64 ConfigBytes = 0;
    for (const FString& ConfigFile : ConfigFiles)
    {
        FProfileSnapshot Source;
        if (!ProfileSnapshot(ConfigFile, 8ll * 1024 * 1024, Source))
            return Reject(TEXT("texture-profile-config-snapshot"));
        ConfigBytes += int64(Source.Bytes.size());
        if (ConfigBytes > 32ll * 1024 * 1024)
            return Reject(TEXT("texture-profile-config-total-budget"));
        for (const auto& Existing : Configs)
            if (Existing.Path.Equals(Source.Path, ESearchCase::IgnoreCase))
                return Reject(TEXT("texture-profile-config-duplicate"));
        P4MapKit::FTextureProfileInventory Inventory;
        P4MapKit::FReadError ReadError;
        if (!P4MapKit::ReadTextureProfileInventory(
                Source.Bytes, Groups, P4MapKit::ETextureGroupContract::Original111_64, Sections, Inventory, ReadError))
            return Reject(ProfileString(ReadError.Code));
        auto FileJson = ProfileSourceJson(Source);
        FileJson->SetNumberField(TEXT("bom_bytes"), double(Inventory.BomBytes));
        FileJson->SetNumberField(TEXT("lines"), double(Inventory.Lines));
        FileJson->SetBoolField(TEXT("profile_inheritance_resolved"), false);
        FileJson->SetBoolField(TEXT("c4_behavior_qualified"), false);
        TArray<TSharedPtr<FJsonValue>> Entries;
        for (const auto& Entry : Inventory.Entries)
        {
            auto Value = MakeShared<FJsonObject>();
            Value->SetStringField(TEXT("section"), ProfileString(Entry.Section));
            Value->SetStringField(TEXT("key"), ProfileString(Entry.Key));
            Value->SetStringField(TEXT("operation"), ProfileString(Entry.Operation));
            Value->SetStringField(TEXT("raw"), ProfileString(Entry.Raw));
            Value->SetStringField(TEXT("value"), ProfileString(Entry.Value));
            Value->SetStringField(TEXT("group"), ProfileString(Entry.Group));
            Value->SetStringField(TEXT("line_ending"), ProfileString(Entry.LineEnding));
            Value->SetNumberField(TEXT("offset"), double(Entry.Offset));
            Value->SetNumberField(TEXT("end"), double(Entry.End));
            Value->SetNumberField(TEXT("line"), double(Entry.Line));
            Value->SetBoolField(TEXT("is_group_row"), Entry.IsGroupRow);
            Value->SetBoolField(TEXT("original_group_known"), Entry.OriginalGroupKnown);
            Value->SetBoolField(TEXT("duplicate_row"), Entry.DuplicateRow);
            TArray<TSharedPtr<FJsonValue>> Members;
            for (const auto& Member : Entry.Members)
            {
                auto Field = MakeShared<FJsonObject>();
                Field->SetStringField(TEXT("key"), ProfileString(Member.Key));
                Field->SetStringField(TEXT("raw"), ProfileString(Member.Raw));
                Field->SetStringField(TEXT("value"), ProfileString(Member.Value));
                Field->SetBoolField(TEXT("duplicate_key"), Member.DuplicateKey);
                Members.Add(MakeShared<FJsonValueObject>(Field));
            }
            Value->SetArrayField(TEXT("members"), Members);
            Entries.Add(MakeShared<FJsonValueObject>(Value));
        }
        FileJson->SetArrayField(TEXT("entries"), Entries);
        ProfileFiles.Add(MakeShared<FJsonValueObject>(FileJson));
        Configs.Add(MoveTemp(Source));
    }
    if (!ProfileUnchanged(Capture))
        return Reject(TEXT("texture-profile-source-changed"));
    for (const auto& Source : Configs)
        if (!ProfileUnchanged(Source))
            return Reject(TEXT("texture-profile-source-changed"));
    TArray<TSharedPtr<FJsonValue>> EnumValues;
    for (const auto& Entry : Enum.Entries)
    {
        auto Value = MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("name"), Entry.Name);
        Value->SetStringField(TEXT("value"), FString::Printf(TEXT("%lld"), Entry.Value));
        EnumValues.Add(MakeShared<FJsonValueObject>(Value));
    }
    Result->SetObjectField(TEXT("capture_source"), ProfileSourceJson(Capture));
    Result->SetStringField(TEXT("declared_source_sha256"), Enum.DeclaredSourceSha256);
    Result->SetStringField(TEXT("declared_captured_at"), Enum.DeclaredCapturedAt);
    Result->SetStringField(TEXT("declared_scope"), Enum.DeclaredScope);
    Result->SetArrayField(TEXT("original_groups"), EnumValues);
    Result->SetArrayField(TEXT("profile_sources"), ProfileFiles);
    Result->SetBoolField(TEXT("valid"), true);
    Result->SetStringField(TEXT("error"), TEXT(""));
    return ProfileResult(Result);
}
